// Particle billboards (gfx/particles.cc, 0.22.7). The mesh is a unit quad; the instance is an
// inst_t: i_pos = position + spin, i_scol = size + rgb, i_misc = alpha, seed, kind, age 0..1,
// i_fx.x = the soft-depth radius. The quad faces the camera and spins in its own plane.
in vec3 a_pos; in vec3 a_nrm; in vec3 a_col; in float a_mat; in vec2 a_rm;
in vec4 i_pos; in vec4 i_scol; in vec4 i_misc; in vec4 i_fx; in vec2 i_anchor;
uniform mat4 u_vp; uniform vec3 u_cam_right; uniform vec3 u_cam_up;
out vec2 v_uv; out vec3 v_col; out vec4 v_misc; out vec3 v_wpos; out float v_size; out float v_soft;
void main(){
  float c = cos(i_pos.w), s = sin(i_pos.w);
  vec2 q = vec2(a_pos.x * c - a_pos.y * s, a_pos.x * s + a_pos.y * c);
// kind 6 (a ring on the water) lies flat in the world; everything else faces the camera
  vec3 ax = i_misc.z > 5.5 ? vec3(1.0, 0.0, 0.0) : u_cam_right, ay = i_misc.z > 5.5 ? vec3(0.0, 0.0, 1.0) : u_cam_up;
  vec3 wp = i_pos.xyz + (ax * q.x + ay * q.y) * i_scol.x;
  v_uv = a_pos.xy; v_col = i_scol.yzw; v_misc = i_misc; v_wpos = wp; v_size = i_scol.x; v_soft = i_fx.x;
  gl_Position = u_vp * vec4(wp, 1.0);
}
