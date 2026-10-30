// The one directional light every 3D pass shares. Declares its own uniforms; set them with
// light_bind() (lighting.c). u_shadow_on > 0.5 enables the cascaded shadow lookup, which reads
// the three cascades packed side by side in one depth atlas.
// Pipelines using this append GLSL_LIGHT_UNIFORMS (gfx/glsl.h) to their uniform list.
#include "common/clouds.glsl"
uniform vec3 u_sun;          /* direction towards the sun, unit */
uniform vec3 u_suncol;       /* linear radiance */
uniform vec3 u_ambsky, u_ambgnd;
uniform sampler2DShadow u_shadowmap; uniform mat4 u_shadowvp0, u_shadowvp1, u_shadowvp2;
uniform vec2 u_shadowsplit; uniform float u_shadow_on; uniform vec2 u_shadow_texel;
uniform vec3 u_shadow_wtexel;   /* world size of one shadow texel, per cascade */
uniform float u_shadow_debug;   /* 1: shadow factor as grayscale, 2: diagnostics rgb (MARE_SHADOW_DEBUG) */
float shadow_at(vec3 wpos, vec3 N, float NdotL, float viewdist) {
  if (u_shadow_on < 0.5) return 1.0;
  int c = viewdist > u_shadowsplit.y ? 2 : (viewdist > u_shadowsplit.x ? 1 : 0);
// normal-offset bias: push the lookup off the surface by a couple of shadow texels, which 
// removes acne on slopes far better than a pure depth bias can
  float wt = c == 2 ? u_shadow_wtexel.z : c == 1 ? u_shadow_wtexel.y : u_shadow_wtexel.x;
  wpos += N * wt * (1.0 + 2.0 * (1.0 - NdotL));
  vec4 lp = c == 2 ? u_shadowvp2 * vec4(wpos, 1.0) : c == 1 ? u_shadowvp1 * vec4(wpos, 1.0) : u_shadowvp0 * vec4(wpos, 1.0);
  vec3 p = lp.xyz / lp.w; p = p * 0.5 + 0.5;
  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return u_shadow_debug > 1.5 ? -1.0 : 1.0;
// the atlas depth range is only ~2*(radius+margin) world units (lighting.c), so this 
// small constant bias is well under the occluder separation of even a rowboat
  float bias = max(0.0012 * (1.0 - NdotL), 0.0004);
  p.x = (p.x + float(c)) / 3.0;                                 /* three cascades side by side */
  float s = 0.0;
// 3x3 taps, each a hardware-PCF (bilinear) depth comparison
  for (int j = -1; j <= 1; ++j) for (int i = -1; i <= 1; ++i)
    s += texture(u_shadowmap, vec3(p.xy + vec2(float(i), float(j)) * u_shadow_texel, p.z - bias));
  return s / 9.0;
}
// `ao` (0..1) occludes the ambient only: the sun term keeps the shadow map (step 9)
vec3 sun_shade_ao(vec3 albedo, vec3 N, vec3 V, vec3 wpos, float rough, float metal, float viewdist, float ao) {
  float sh = shadow_at(wpos, N, max(dot(N, u_sun), 0.0), viewdist);
  if (u_shadow_debug > 1.5) return sh < 0.0 ? vec3(1.0, 0.0, u_shadow_on) : vec3(0.0, sh, u_shadow_on);
// red: out of cascade bounds; green: factor; blue: enabled
  if (u_shadow_debug > 0.5) return vec3(sh * 0.8 + 0.1);
  sh *= cloud_shadow(wpos, u_sun);
  return pbr_direct(albedo, N, V, u_sun, rough, metal, u_suncol) * sh
       + pbr_ambient(albedo, N, metal, u_ambsky, u_ambgnd) * ao;
}
vec3 sun_shade(vec3 albedo, vec3 N, vec3 V, vec3 wpos, float rough, float metal, float viewdist) { return sun_shade_ao(albedo, N, V, wpos, rough, metal, viewdist, 1.0); }
