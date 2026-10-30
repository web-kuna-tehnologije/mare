in vec2 a_pos;
#include "sea/vs_common.glsl"
void main(){
  v_wpos = vec3(a_pos.x, 0.0, a_pos.y);
  v_nrm = vec3(0.0, 1.0, 0.0); v_slope = 0.0; v_h = height_at(a_pos); v_amp = 0.0; v_edge = 0.0; v_jac = 1.0; v_crest = 0.0; v_shipw = 0.0;
  gl_Position = u_vp * vec4(v_wpos, 1.0);
}
