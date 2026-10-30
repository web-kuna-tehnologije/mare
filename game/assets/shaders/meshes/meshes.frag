in vec3 v_nrm; in vec3 v_col; in vec3 v_wpos; in float v_sel; in vec2 v_rm; in float v_dither;
uniform vec3 u_campos; uniform vec3 u_sky; uniform float u_mapness; uniform sampler2D u_grain;
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/nmap.glsl"
#include "common/fog.glsl"
#include "common/dither.glsl"
void main(){
  dither_clip(v_dither);
  vec3 n = normalize(v_nrm);
  vec3 albedo = srgb_to_linear(v_col);
  vec3 vdir = normalize(u_campos - v_wpos);
// Two-sided shading: sails, flags and roof slopes are single-quad surfaces drawn without
// backface culling, so flip the flat normal towards the viewer instead of letting whole
// faces fall into the ambient-only floor.
  if (dot(n, vdir) < 0.0) n = -n;
// triplanar surface grain, fading with distance so far meshes keep their clean silhouette;
// rougher materials (wood, cloth, stone) get more relief than painted ones
  float dist0 = distance(v_wpos, u_campos);
  float grain = v_rm.x * 0.42 * (1.0 - smoothstep(30.0, 120.0, dist0));
  if (grain > 0.01) n = nmap_triplanar(u_grain, v_wpos * 1.6, n, 1.0, grain);
  float dist = dist0;
  vec3 c = sun_shade(albedo, n, vdir, v_wpos, v_rm.x, v_rm.y, dist);
  c = mix(c, srgb_to_linear(vec3(1.0,0.95,0.6)) * 1.6, v_sel * 0.35);
// selection tint
  c = fog_apply(c, dist, u_mapness, srgb_to_linear(u_sky));
  fragColor = vec4(c, 1.0);
}
