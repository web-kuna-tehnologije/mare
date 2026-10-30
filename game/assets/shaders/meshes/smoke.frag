in vec3 v_nrm; in vec3 v_col; in vec3 v_wpos; in float v_sel; in vec2 v_rm; in float v_dither;
uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness;
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/fog.glsl"
void main(){
  vec3 n = normalize(v_nrm);
  vec3 vdir = normalize(u_campos - v_wpos);
  if (dot(n, vdir) < 0.0) n = -n;
  float facing = max(dot(n, vdir), 0.0);
  vec3 albedo = srgb_to_linear(v_col);
  float sh = shadow_at(v_wpos, n, max(dot(n, u_sun), 0.0), distance(v_wpos, u_campos));
// soft, mostly ambient lighting: smoke scatters rather than reflects
  vec3 c = albedo * (u_ambsky * 0.9 + u_ambgnd * 0.4 + u_suncol * 0.16 * sh * max(dot(n, u_sun), 0.0));
  c = fog_apply(c, distance(v_wpos, u_campos), u_mapness, srgb_to_linear(u_sky));
  fragColor = vec4(c, smoothstep(0.0, 0.75, facing) * 0.72);
}
