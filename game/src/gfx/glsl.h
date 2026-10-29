// Shared GLSL bits. v3 creates a compatibility context on Windows/Linux and a 3.2 core
// context on macOS, so shaders avoid layout qualifiers and bind attributes by name.
//
// Colour pipeline: author colours (vertex colours, nation colours, palettes) are sRGB. Every
// shader converts them to linear with srgb_to_linear() before any lighting math, writes linear
// radiance into the RGBA16F HDR target, and hdr.c encodes to sRGB after tonemapping.

#ifndef MARE_GLSL_H
#define MARE_GLSL_H

#define GLSL_VERSION ifdef(V3_MACOS, "#version 150\n", "#version 130\n")

// u_fog: x = density multiplier, y = start distance (weather.c; 1 and 250 in clear air)
#define GLSL_FOG \
    "uniform vec2 u_fog;\n" \
    "vec3 fog_apply(vec3 c, float dist, float mapness, vec3 sky) {\n" \
    "  float f = 1.0 - exp(-max(dist - u_fog.y, 0.0) * 0.0016 * u_fog.x);\n" \
    "  return mix(c, sky, f*(1.0-mapness)*0.85);\n" \
    "}\n"

// sRGB <-> linear (exact piecewise transfer function)
#define GLSL_COLOR \
    "vec3 srgb_to_linear(vec3 c) {\n" \
    "  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n" \
    "}\n" \
    "vec3 linear_to_srgb(vec3 c) {\n" \
    "  c = max(c, vec3(0.0));\n" \
    "  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(vec3(0.0031308), c));\n" \
    "}\n"

// Cook-Torrance GGX + Smith + Schlick, Lambert diffuse. Roughness and metalness are linear
// 0..1 inputs; albedo must already be linear.
#define GLSL_PBR \
    "const float PBR_PI = 3.14159265;\n" \
    "vec3 pbr_direct(vec3 albedo, vec3 N, vec3 V, vec3 L, float rough, float metal, vec3 lightcol) {\n" \
    "  float NdotL = max(dot(N, L), 0.0); if (NdotL <= 0.0) return vec3(0.0);\n" \
    "  vec3 H = normalize(V + L);\n" \
    "  float NdotV = max(dot(N, V), 1e-4), NdotH = max(dot(N, H), 0.0), VdotH = max(dot(V, H), 0.0);\n" \
    "  float a = max(rough * rough, 1e-3), a2 = a * a;\n" \
    "  float den = NdotH * NdotH * (a2 - 1.0) + 1.0;\n" \
    "  float D = a2 / max(PBR_PI * den * den, 1e-7);\n" \
    "  float k = a * 0.5;\n" \
    "  float G = (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));\n" \
    "  vec3 F0 = mix(vec3(0.04), albedo, metal);\n" \
    "  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);\n" \
    "  vec3 spec = (D * G) * F / max(4.0 * NdotV * NdotL, 1e-5);\n" \
    "  vec3 diff = (1.0 - F) * (1.0 - metal) * albedo / PBR_PI;\n" \
    "  return (diff + spec) * lightcol * NdotL;\n" \
    "}\n" \
    "vec3 pbr_ambient(vec3 albedo, vec3 N, float metal, vec3 sky, vec3 ground) {\n" \
    "  vec3 amb = mix(ground, sky, N.y * 0.5 + 0.5);\n" \
    "  return albedo * amb * (1.0 - 0.5 * metal);\n" \
    "}\n"

// Cloud layer (gfx/sky.c, DESIGN_VISUAL.md step 8): value-noise fbm on a plane u_cloud.w units
// up, drifting by u_cloud.xy; u_cloud.z is the cover. The sky pass draws it, and sun_shade
// darkens the direct term where a cloud sits between the surface and the sun.
#define GLSL_CLOUDS \
    "uniform vec4 u_cloud;\n" \
    "float cl_hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n" \
    /* Gradient noise, not value noise. Value noise interpolates one number per lattice corner, \
       so a threshold through it follows the lattice and paints the ground in soft rectangles - \
       which is exactly what the cloud shadows did. Gradient noise interpolates a dot product \
       with a random direction per corner: it is zero at every corner, so no lattice structure \
       survives. Quintic fade for a continuous second derivative. */ \
    "vec2 cl_grad(vec2 i) { float a = cl_hash(i) * 6.2831853; return vec2(cos(a), sin(a)); }\n" \
    "float cl_noise(vec2 p) {\n" \
    "  vec2 i = floor(p), f = fract(p);\n" \
    "  vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);\n" \
    "  float a = dot(cl_grad(i), f);\n" \
    "  float b = dot(cl_grad(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0));\n" \
    "  float c = dot(cl_grad(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0));\n" \
    "  float d = dot(cl_grad(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0));\n" \
    "  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.78 + 0.5;\n" \
    "}\n" \
    /* Each octave is rotated as well as scaled, and the sum is normalised by its own amplitude. \
       Without the rotation the lattice of the coarsest octave stays axis-aligned and paints the \
       ground in soft rectangles; without the normalisation the 3-octave shadow field and the \
       5-octave visible one cover different fractions of the sky and the shadows stop matching \
       the clouds casting them. */ \
    "float cl_fbm(vec2 p, int octaves) {\n" \
    "  float f = 0.0, a = 0.55, norm = 0.0;\n" \
    "  for (int i = 0; i < octaves; ++i) {\n" \
    "    f += a * cl_noise(p); norm += a;\n" \
    "    p = mat2(0.80, 0.60, -0.60, 0.80) * (p * 2.07) + vec2(13.1, 7.3); a *= 0.62;\n" \
    "  }\n" \
    "  return f / max(norm, 1e-4);\n" \
    "}\n" \
    "float cloud_density(vec2 xz, int octaves) {\n" \
    /* the base lattice is rotated off the world axes as well, so even the coarsest feature \
       edges do not run north-south */ \
    "  vec2 q = mat2(0.86, 0.51, -0.51, 0.86) * (xz * 0.017) + u_cloud.xy;\n" \
    "  float f = cl_fbm(q, octaves);\n" \
    "  float thr = 0.74 - u_cloud.z * 0.52;\n" \
    "  return smoothstep(thr, thr + 0.26, f);\n" \
    "}\n" \
    "float cloud_shadow(vec3 wpos, vec3 sun) {\n" \
    "  if (u_cloud.z <= 0.001) return 1.0;\n" \
    "  vec2 xz = wpos.xz + sun.xz / max(sun.y, 0.15) * (u_cloud.w - wpos.y);\n"   /* where the sun's ray through this point meets the cloud plane */ \
    "  return 1.0 - 0.62 * cloud_density(xz, 4);\n" \
    "}\n"

// The one directional light every 3D pass shares. Declares its own uniforms; set them with
// light_bind() (lighting.c). u_shadow_on > 0.5 enables the cascaded shadow lookup, which reads
// the three cascades packed side by side in one depth atlas.
#define GLSL_LIGHT \
    GLSL_CLOUDS \
    "uniform vec3 u_sun;          /* direction towards the sun, unit */\n" \
    "uniform vec3 u_suncol;       /* linear radiance */\n" \
    "uniform vec3 u_ambsky, u_ambgnd;\n" \
    "uniform sampler2DShadow u_shadowmap; uniform mat4 u_shadowvp0, u_shadowvp1, u_shadowvp2;\n" \
    "uniform vec2 u_shadowsplit; uniform float u_shadow_on; uniform vec2 u_shadow_texel;\n" \
    "uniform vec3 u_shadow_wtexel;   /* world size of one shadow texel, per cascade */\n" \
    "uniform float u_shadow_debug;   /* 1: shadow factor as grayscale, 2: diagnostics rgb (MARE_SHADOW_DEBUG) */\n" \
    "float shadow_at(vec3 wpos, vec3 N, float NdotL, float viewdist) {\n" \
    "  if (u_shadow_on < 0.5) return 1.0;\n" \
    "  int c = viewdist > u_shadowsplit.y ? 2 : (viewdist > u_shadowsplit.x ? 1 : 0);\n" \
    /* normal-offset bias: push the lookup off the surface by a couple of shadow texels, which \
       removes acne on slopes far better than a pure depth bias can */ \
    "  float wt = c == 2 ? u_shadow_wtexel.z : c == 1 ? u_shadow_wtexel.y : u_shadow_wtexel.x;\n" \
    "  wpos += N * wt * (1.0 + 2.0 * (1.0 - NdotL));\n" \
    "  vec4 lp = c == 2 ? u_shadowvp2 * vec4(wpos, 1.0) : c == 1 ? u_shadowvp1 * vec4(wpos, 1.0) : u_shadowvp0 * vec4(wpos, 1.0);\n" \
    "  vec3 p = lp.xyz / lp.w; p = p * 0.5 + 0.5;\n" \
    "  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return u_shadow_debug > 1.5 ? -1.0 : 1.0;\n" \
    /* the atlas depth range is only ~2*(radius+margin) world units (lighting.c), so this \
       small constant bias is well under the occluder separation of even a rowboat */ \
    "  float bias = max(0.0012 * (1.0 - NdotL), 0.0004);\n" \
    "  p.x = (p.x + float(c)) / 3.0;                                 /* three cascades side by side */\n" \
    "  float s = 0.0;\n" \
    /* 3x3 taps, each a hardware-PCF (bilinear) depth comparison */ \
    "  for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i)\n" \
    "    s += texture(u_shadowmap, vec3(p.xy + vec2(float(i), float(j)) * u_shadow_texel, p.z - bias));\n" \
    "  return s / 9.0;\n" \
    "}\n" \
    /* `ao` (0..1) occludes the ambient only: the sun term keeps the shadow map (step 9) */ \
    "vec3 sun_shade_ao(vec3 albedo, vec3 N, vec3 V, vec3 wpos, float rough, float metal, float viewdist, float ao) {\n" \
    "  float sh = shadow_at(wpos, N, max(dot(N, u_sun), 0.0), viewdist);\n" \
    "  if (u_shadow_debug > 1.5) return sh < 0.0 ? vec3(1.0, 0.0, u_shadow_on) : vec3(0.0, sh, u_shadow_on);\n"   /* red: out of cascade bounds; green: factor; blue: enabled */ \
    "  if (u_shadow_debug > 0.5) return vec3(sh * 0.8 + 0.1);\n" \
    "  sh *= cloud_shadow(wpos, u_sun);\n" \
    "  return pbr_direct(albedo, N, V, u_sun, rough, metal, u_suncol) * sh\n" \
    "       + pbr_ambient(albedo, N, metal, u_ambsky, u_ambgnd) * ao;\n" \
    "}\n" \
    "vec3 sun_shade(vec3 albedo, vec3 N, vec3 V, vec3 wpos, float rough, float metal, float viewdist) { return sun_shade_ao(albedo, N, V, wpos, rough, metal, viewdist, 1.0); }\n"

// Tangent-space normal mapping (detail.c textures). The procedural meshes carry no UVs, so
// nmap_triplanar perturbs the flat normal by sampling the grain map along the three world axes,
// weighted by the normal; the sea has a trivial tangent frame (a flat y-up plane).
#define GLSL_NMAP \
    "vec3 nmap_decode(vec3 t) { return t * 2.0 - 1.0; }\n" \
    /* UDN-style triplanar: add each projection's tangent-space xy, swizzled onto its world \
       plane and weighted by the normal, then renormalize */ \
    "vec3 nmap_triplanar(sampler2D tex, vec3 wpos, vec3 N, float tiling, float strength) {\n" \
    "  vec3 w = abs(N); w = w*w*w*w; w /= (w.x + w.y + w.z);\n" \
    "  vec3 tx = nmap_decode(texture(tex, wpos.zy * tiling).rgb);\n" \
    "  vec3 ty = nmap_decode(texture(tex, wpos.xz * tiling).rgb);\n" \
    "  vec3 tz = nmap_decode(texture(tex, wpos.xy * tiling).rgb);\n" \
    "  vec3 d = vec3(0.0, tx.y, tx.x) * w.x + vec3(ty.x, 0.0, ty.y) * w.y + vec3(tz.x, tz.y, 0.0) * w.z;\n" \
    "  return normalize(N + d * strength);\n" \
    "}\n"

// LOD cross-fade (gfx/lod.h, inst_t.dither): an ordered 4x4 Bayer threshold per pixel. d > 0
// discards the pixels whose threshold is below d (the level fading out keeps a 1-d share),
// d < 0 discards the complement (the level fading in keeps a -d share). Both levels of a fading
// instance therefore partition the screen exactly, in every pass that uses this (main, shadow).
#define GLSL_DITHER \
    /* the 4x4 Bayer matrix as bit arithmetic (GLSL 1.30 has no const arrays worth trusting): \
       M2(x,y) = 2*((x^y)&1) + (y&1); M4 = 4*M2(x&1,y&1) + M2(x>>1,y>>1) */ \
    "float dither_bayer4(vec2 p) {\n" \
    "  int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));\n" \
    "  int m = 4 * (2 * ((x ^ y) & 1) + (y & 1)) + (2 * (((x >> 1) ^ (y >> 1)) & 1) + ((y >> 1) & 1));\n" \
    "  return (float(m) + 0.5) / 16.0;\n" \
    "}\n" \
    "void dither_clip(float d) {\n" \
    "  if (d == 0.0) return;\n" \
    "  float b = dither_bayer4(gl_FragCoord.xy);\n" \
    "  if (d > 0.0 ? b < d : b >= -d) discard;\n" \
    "}\n"

// ---------------------------------------------------------------------------
// Gerstner waves (gfx/sea.c). Shared by the sea pass, which displaces its grid by them, and by
// the instanced mesh pass, which lifts and tilts every floating instance onto the same surface -
// otherwise ships would sail through the swell instead of riding it.
//
// Wavelengths are world units (1 unit is roughly 25 m: a ship of the line is 2.09 units long),
// so the set below is a 90 m swell, a 55 m cross sea and two shorter chops. `scale` stretches
// the whole field for scenes with different units - the detail view passes DV_SHIP_SCALE, which
// is exactly the ratio between its units and the map's, so the sea keeps its physical size
// relative to the ships in both.
// Gerstner wave field (gfx/sea.c, DESIGN_VISUAL.md step 4). The wave set is not authored here:
// sea.c derives it from the wind at the sea's centre - directions spread around the wind
// direction, wavelengths and heights growing with its strength - and uploads it as
// u_wave/u_wave_phase. Phase is *accumulated on the CPU* rather than computed as speed*time.
//
// Two sets, cross-faded by u_wave_blend. A wave set must never change continuously while it is
// on screen: a direction that rotates by even a degree sweeps its crests sideways by (distance
// from the phase origin) x (angle), which hundreds of units from the origin is faster than the
// waves travel - the "swell speeds up when I pan the camera" artefact. So when the wind at the
// look-at point has changed enough, sea.c builds a *new* static set and fades the old one out
// over several seconds; two static fields blending by amplitude move no crest anywhere.
#define SEA_WAVES 5
#define SEA_WAVE_SLOTS 10
#define GLSL_WAVES \
    "uniform vec4 u_wave[10];\n"           /* xy = unit direction, z = wavelength, w = steepness weight; 0-4 set A, 5-9 set B */ \
    "uniform float u_wave_phase[10];\n"    /* accumulated phase, radians */ \
    "uniform float u_wave_blend;\n"        /* 0 = set A only .. 1 = set B only */ \
    "const float WAVE_STEEP = 0.026;\n" \
    "const float WAVE_PINCH = 0.55;\n" \
    /* Gerstner sum at world point p: returns the displacement, the two surface tangents (so \
       the caller gets an exact normal with no finite differences) and a crest factor. \
       `cell` is the world size of one grid cell at this vertex, 0 where the surface is not a \
       grid (a ship riding the swell). A wave shorter than a few cells cannot be represented by \
       that grid: sampling it anyway aliases it into a pattern that slides as the grid moves \
       with the camera. Those waves are faded out instead, so what is drawn is only what is \
       sampled. */ \
    "vec3 wave_sum(vec2 p, float amp, float scale, float cell, out vec3 tan_x, out vec3 tan_z, out float crest) {\n" \
    "  vec3 d = vec3(0.0); tan_x = vec3(1.0, 0.0, 0.0); tan_z = vec3(0.0, 0.0, 1.0); crest = 0.0;\n" \
    "  if (amp <= 0.0) return d;\n" \
    "  float wsum = 1e-4;\n" \
    "  for (int i = 0; i < 10; ++i) {\n" \
    "    float setw = i < 5 ? 1.0 - u_wave_blend : u_wave_blend;\n" \
    "    if (setw <= 0.001) continue;\n" \
    "    vec2 dir = u_wave[i].xy;\n" \
    "    float len = u_wave[i].z * scale;\n" \
    "    float fade = cell > 0.0 ? smoothstep(2.0 * cell, 4.0 * cell, len) : 1.0;\n" \
    "    if (fade <= 0.001) continue;\n" \
    "    float k = 6.28318 / max(len, 1e-4);\n" \
    "    float w = u_wave[i].w * fade * setw;\n" \
    "    float a = w * amp * len * WAVE_STEEP;\n" \
    "    float ph = k * dot(dir, p) - u_wave_phase[i];\n" \
    "    float s = sin(ph), c = cos(ph);\n" \
    /* Gerstner pinch. q = 1 is the textbook trochoid, whose crests come to a point - which at \
       the amplitudes a running sea uses reads as rows of spikes rather than water. Holding it \
       well below 1 rounds the crests while keeping the forward lean that makes a swell look \
       like it is travelling. */ \
    "    float q = min(u_wave[i].w, 1.0) * WAVE_PINCH;\n" \
    "    d.xz += dir * (q * a * c);\n" \
    "    d.y  += a * s;\n" \
    "    tan_x += vec3(-dir.x * dir.x * q * a * k * s, dir.x * a * k * c, -dir.x * dir.y * q * a * k * s);\n" \
    "    tan_z += vec3(-dir.x * dir.y * q * a * k * s, dir.y * a * k * c, -dir.y * dir.y * q * a * k * s);\n" \
    "    crest += max(s, 0.0) * w;\n" \
    "    wsum += w;\n" \
    "  }\n" \
    "  crest = clamp((crest / wsum - 0.58) * 3.7, 0.0, 1.0);\n" \
    "  return d;\n" \
    "}\n" \
    "float wave_depth_fade(sampler2D hm, vec2 p, vec4 geo, float wk, float scale, float local_depth) {\n" \
    "  float dep = local_depth;\n" \
    "  if (local_depth <= 0.0) {\n" \
    "    float lon = p.x / wk, lat = -p.y / wk;\n" \
    "    vec2 uv = vec2((lon - geo.x)/(geo.y-geo.x), (geo.w - lat)/(geo.w-geo.z));\n" \
    "    dep = max(-texture(hm, uv).r, 0.0);\n" \
    "  }\n" \
    "  return smoothstep(0.0, 1.1 * scale, dep);\n" \
    "}\n"

// uniform names every pipeline that includes GLSL_WAVES must pre-resolve
#define GLSL_WAVE_UNIFORMS "u_wave", "u_wave_phase", "u_wave_blend"

// uniform names declared by GLSL_LIGHT, to append to a pipeline's uniform list
#define GLSL_LIGHT_UNIFORMS \
    "u_sun", "u_suncol", "u_ambsky", "u_ambgnd", "u_shadowmap", \
    "u_shadowvp0", "u_shadowvp1", "u_shadowvp2", "u_shadowsplit", "u_shadow_on", "u_shadow_texel", "u_shadow_wtexel", "u_shadow_debug", "u_fog", "u_cloud"

#endif
