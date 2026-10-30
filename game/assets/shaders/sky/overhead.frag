in vec2 v_uv; out vec4 fragColor;
uniform mat4 u_inv_vp; uniform vec3 u_campos; uniform vec3 u_sun; uniform float u_daylight; uniform float u_overcast;
uniform sampler2D u_depth; uniform float u_far;
#include "common/color.glsl"
#include "common/clouds.glsl"
void main(){
  if (u_cloud.z <= 0.001 || u_campos.y <= u_cloud.w + 2.0) discard;
  vec4 w = u_inv_vp * vec4(v_uv * 2.0 - 1.0, 1.0, 1.0);
  vec3 dir = normalize(w.xyz / w.w - u_campos);
  if (dir.y > -0.02) discard;
  float t = (u_cloud.w - u_campos.y) / dir.y;
  if (t <= 0.0 || t > u_far) discard;
  float z = texture(u_depth, v_uv).r;
  vec4 sp = u_inv_vp * vec4(v_uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0);
  float scene_t = z >= 0.99999 ? 1e9 : distance(sp.xyz / sp.w, u_campos);
  if (t > scene_t) discard;
// the ground is in front of the cloud here
  vec2 cxz = u_campos.xz + dir.xz * t;
  float dens = cloud_density(cxz, 5);
  if (dens <= 0.002) discard;
  float fade = 1.0 - smoothstep(u_far * 0.55, u_far, t);
  float sunup = clamp(u_sun.y * 1.6, 0.0, 1.0);
  vec3 lit = mix(vec3(0.55, 0.58, 0.64), vec3(1.0, 0.99, 0.97), sunup) * max(u_daylight, 0.25);
  lit = mix(lit, lit * vec3(0.76, 0.78, 0.84), u_overcast * 0.6);
  fragColor = vec4(lit, dens * fade * 0.92);
}
