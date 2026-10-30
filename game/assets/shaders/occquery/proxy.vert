in vec3 a_pos;
uniform mat4 u_vp; uniform vec4 u_pos; uniform vec4 u_bmin; uniform vec4 u_bmax;
// u_pos: xyz + heading, u_bmin/u_bmax: bounds (w: scale)
void main(){
  vec3 p = mix(u_bmin.xyz, u_bmax.xyz, a_pos) * u_bmin.w;
  float c = cos(u_pos.w), s = sin(u_pos.w);
  vec3 wp = vec3(p.x*c + p.z*s, p.y, -p.x*s + p.z*c) + u_pos.xyz;
  gl_Position = u_vp * vec4(wp, 1.0);
}
