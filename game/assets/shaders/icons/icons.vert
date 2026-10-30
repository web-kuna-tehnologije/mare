in vec2 a_corner; in vec4 i_pos; in vec4 i_misc; in vec4 i_col;
uniform mat4 u_vp; uniform vec2 u_viewport;
out vec2 v_uv; out vec4 v_col; out float v_shape; out float v_outline;
void main(){
  vec4 clip = u_vp * vec4(i_pos.xyz, 1.0);
  float h = i_pos.w; float c = cos(h), s = sin(h);
  vec2 cr = vec2(a_corner.x*c - a_corner.y*s, a_corner.x*s + a_corner.y*c);
  clip.xy += cr * (i_misc.x / u_viewport) * 2.0 * clip.w;
  gl_Position = clip; v_uv = a_corner; v_col = vec4(i_col.rgb, i_misc.z); v_shape = i_misc.y; v_outline = i_misc.w;
}
