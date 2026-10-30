layout(quads, fractional_odd_spacing, ccw) in;
// no derivatives in an evaluation stage: every lookup is an explicit LOD 0 (the same Intel
// link crash the sea's evaluation stage works around - see sea/sea.tese)
#define texture(s, uv) textureLod(s, uv, 0.0)
in vec2 tc_uv[];
uniform mat4 u_vp; uniform vec2 u_origin; uniform float u_size; uniform float u_flatten;
uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;
#include "terrain/detail.glsl"
out vec3 v_wpos; out vec2 v_uv; out float v_h;
void main(){
  float tu = gl_TessCoord.x, tv = gl_TessCoord.y;
  vec2 cp = mix(mix(tc_uv[0], tc_uv[1], tu), mix(tc_uv[3], tc_uv[2], tu), tv);
// from here the body is terrain.vert's, minus the skirt term (matched edge levels need none)
  vec2 xz = u_origin + cp * u_size;
  float lon = xz.x / u_k, lat = -xz.y / u_k;
  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));
  float h = texture(u_hm, uv).r;
  h += det_at(xz) * (1.0 - u_flatten);
  float hf = mix(h, (h > 0.0 ? 0.12 : -0.5), u_flatten);
  v_wpos = vec3(xz.x, hf, xz.y); v_uv = uv; v_h = h;
  gl_Position = u_vp * vec4(v_wpos, 1.0);
}
