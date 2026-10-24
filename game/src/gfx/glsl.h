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
    "uniform sampler2D u_shadowmap; uniform mat4 u_shadowvp0, u_shadowvp1, u_shadowvp2;\n" \
    "uniform vec2 u_shadowsplit; uniform float u_shadow_on; uniform vec2 u_shadow_texel;\n" \
    "uniform vec3 u_shadow_wtexel;   /* world size of one shadow texel, per cascade */\n" \
    "float shadow_at(vec3 wpos, vec3 N, float NdotL, float viewdist) {\n" \
    "  if (u_shadow_on < 0.5) return 1.0;\n" \
    "  int c = viewdist > u_shadowsplit.y ? 2 : (viewdist > u_shadowsplit.x ? 1 : 0);\n" \
    /* normal-offset bias: push the lookup off the surface by a couple of shadow texels, which \
       removes acne on slopes far better than a pure depth bias can */ \
    "  float wt = c == 2 ? u_shadow_wtexel.z : c == 1 ? u_shadow_wtexel.y : u_shadow_wtexel.x;\n" \
    "  wpos += N * wt * (1.0 + 2.0 * (1.0 - NdotL));\n" \
    "  vec4 lp = c == 2 ? u_shadowvp2 * vec4(wpos, 1.0) : c == 1 ? u_shadowvp1 * vec4(wpos, 1.0) : u_shadowvp0 * vec4(wpos, 1.0);\n" \
    "  vec3 p = lp.xyz / lp.w; p = p * 0.5 + 0.5;\n" \
    "  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;\n" \
    "  float bias = max(0.0012 * (1.0 - NdotL), 0.0004) * (1.0 + float(c) * 1.5);\n" \
    "  p.x = (p.x + float(c)) / 3.0;                                 /* three cascades side by side */\n" \
    "  float s = 0.0;\n" \
    "  for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i)\n" \
    "    s += texture(u_shadowmap, p.xy + vec2(float(i), float(j)) * u_shadow_texel).r < p.z - bias ? 0.0 : 1.0;\n" \
    "  return s / 9.0;\n" \
    "}\n" \
    "vec3 sun_shade(vec3 albedo, vec3 N, vec3 V, vec3 wpos, float rough, float metal, float viewdist) {\n" \
    "  float sh = shadow_at(wpos, N, max(dot(N, u_sun), 0.0), viewdist);\n" \
    "  return pbr_direct(albedo, N, V, u_sun, rough, metal, u_suncol) * sh\n" \
    "       + pbr_ambient(albedo, N, metal, u_ambsky, u_ambgnd);\n" \
    "}\n"

// uniform names declared by GLSL_LIGHT, to append to a pipeline's uniform list
#define GLSL_LIGHT_UNIFORMS \
    "u_sun", "u_suncol", "u_ambsky", "u_ambgnd", "u_shadowmap", \
    "u_shadowvp0", "u_shadowvp1", "u_shadowvp2", "u_shadowsplit", "u_shadow_on", "u_shadow_texel", "u_shadow_wtexel"

#endif
