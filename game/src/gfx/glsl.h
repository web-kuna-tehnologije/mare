// Shared GLSL bits. v3 creates a compatibility context on Windows/Linux and a 3.2 core
// context on macOS, so shaders avoid layout qualifiers and bind attributes by name.
//
// Colour pipeline: author colours (vertex colours, nation colours, palettes) are sRGB. Every
// shader converts them to linear with srgb_to_linear() before any lighting math, writes linear
// radiance into the RGBA16F HDR target, and hdr.c encodes to sRGB after tonemapping.

#ifndef MARE_GLSL_H
#define MARE_GLSL_H

#define GLSL_VERSION ifdef(V3_MACOS, "#version 150\n", "#version 130\n")

#define GLSL_FOG \
    "vec3 fog_apply(vec3 c, float dist, float mapness, vec3 sky) {\n" \
    "  float f = 1.0 - exp(-max(dist-250.0,0.0)*0.0016);\n" \
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

// The one directional light every 3D pass shares. Declares its own uniforms; set them with
// light_bind() (lighting.c). u_shadow_on > 0.5 enables the cascaded shadow lookup, which reads
// the three cascades packed side by side in one depth atlas.
#define GLSL_LIGHT \
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
    "vec3 sun_shade(vec3 albedo, vec3 N, vec3 V, vec3 wpos, float rough, float metal, float viewdist) {\n" \
    "  float sh = shadow_at(wpos, N, max(dot(N, u_sun), 0.0), viewdist);\n" \
    "  if (u_shadow_debug > 1.5) return sh < 0.0 ? vec3(1.0, 0.0, u_shadow_on) : vec3(0.0, sh, u_shadow_on);\n"   /* red: out of cascade bounds; green: factor; blue: enabled */ \
    "  if (u_shadow_debug > 0.5) return vec3(sh * 0.8 + 0.1);\n" \
    "  return pbr_direct(albedo, N, V, u_sun, rough, metal, u_suncol) * sh\n" \
    "       + pbr_ambient(albedo, N, metal, u_ambsky, u_ambgnd);\n" \
    "}\n"

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
// sea.c derives it every frame from the wind at the sea's centre - directions spread around the
// wind direction, wavelengths and heights growing with its strength - and uploads it as
// u_wave/u_wave_phase. The phase is *accumulated on the CPU* rather than computed as speed*time,
// so a wave whose length changes because the wind changed keeps its crests where they were
// instead of jumping (and nothing drifts as RF.time grows large).
#define SEA_WAVES 5
#define GLSL_WAVES \
    "uniform vec4 u_wave[5];\n"            /* xy = unit direction, z = wavelength, w = steepness weight */ \
    "uniform float u_wave_phase[5];\n"     /* accumulated phase, radians */ \
    "uniform vec2 u_wind;\n"               /* unit wind direction (the surface detail scrolls along it) */ \
    "const float WAVE_STEEP = 0.030;\n" \
    /* Gerstner sum at world point p: returns the displacement, the two surface tangents (so \
       the caller gets an exact normal with no finite differences) and a crest factor. \
       `cell` is the world size of one grid cell at this vertex, 0 where the surface is not a \
       grid (a ship riding the swell). A wave shorter than a few cells cannot be represented by \
       that grid: sampling it anyway aliases it into a pattern that slides as the grid moves \
       with the camera, which is what made the swell appear to speed up and slow down while \
       panning. Those waves are faded out instead, so what is drawn is only what is sampled. */ \
    "vec3 wave_sum(vec2 p, float amp, float scale, float cell, out vec3 tan_x, out vec3 tan_z, out float crest) {\n" \
    "  vec3 d = vec3(0.0); tan_x = vec3(1.0, 0.0, 0.0); tan_z = vec3(0.0, 0.0, 1.0); crest = 0.0;\n" \
    "  if (amp <= 0.0) return d;\n" \
    "  float wsum = 1e-4;\n" \
    "  for (int i = 0; i < 5; ++i) {\n" \
    "    vec2 dir = u_wave[i].xy;\n" \
    "    float len = u_wave[i].z * scale;\n" \
    "    float fade = cell > 0.0 ? smoothstep(2.0 * cell, 4.0 * cell, len) : 1.0;\n" \
    "    if (fade <= 0.001) continue;\n" \
    "    float k = 6.28318 / max(len, 1e-4);\n" \
    "    float w = u_wave[i].w * fade;\n" \
    "    float a = w * amp * len * WAVE_STEEP;\n" \
    "    float ph = k * dot(dir, p) - u_wave_phase[i];\n" \
    "    float s = sin(ph), c = cos(ph);\n" \
    "    float q = min(u_wave[i].w, 1.0);\n" \
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
#define GLSL_WAVE_UNIFORMS "u_wave", "u_wave_phase", "u_wind"

// uniform names declared by GLSL_LIGHT, to append to a pipeline's uniform list
#define GLSL_LIGHT_UNIFORMS \
    "u_sun", "u_suncol", "u_ambsky", "u_ambgnd", "u_shadowmap", \
    "u_shadowvp0", "u_shadowvp1", "u_shadowvp2", "u_shadowsplit", "u_shadow_on", "u_shadow_texel", "u_shadow_wtexel", "u_shadow_debug"

#endif
