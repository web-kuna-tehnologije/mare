in vec3 a_pos;
// u, v, skirt
uniform mat4 u_vp; uniform vec2 u_origin; uniform float u_size; uniform float u_flatten;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;
#include "terrain/rings.glsl"
out vec3 v_wpos; out vec2 v_uv; out float v_h;
void main(){
  vec2 xz = u_origin + a_pos.xy * u_size;
  float lon = xz.x / u_k, lat = -xz.y / u_k;
  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));
  float h = texture(u_hm, uv).r;
  h += det_at(xz) * (1.0 - u_flatten);
  float hf = mix(h, (h > 0.0 ? 0.12 : -0.5), u_flatten);
  v_wpos = vec3(xz.x, hf - a_pos.z * 3.0, xz.y); v_uv = uv; v_h = h;
  gl_Position = u_vp * vec4(v_wpos, 1.0);
}
