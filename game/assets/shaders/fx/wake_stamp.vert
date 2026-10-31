// One ship's churn, painted into the wake map (gfx/wake.cc). The quad is her own box - two
// hull lengths square, on her heading - mapped from world coordinates into the map's rect.
in vec2 a_pos;
uniform vec4 u_rect;    // x0, z0, 1/extent, 1/extent
uniform vec4 u_quad;    // centre x, z, heading, hull length
out vec2 v_local;       // along, across - in hull lengths, from the box centre
void main(){
  float c = cos(u_quad.z), s = -sin(u_quad.z);   // forward = (cos h, -sin h), as the scene uses
  float L = max(u_quad.w, 0.01);
  vec2 d = a_pos * L;                             // the box is 2L x 2L
  vec2 w = u_quad.xy + vec2(d.x * c - d.y * s, d.x * s + d.y * c);
  v_local = a_pos;
  vec2 uv = (w - u_rect.xy) * u_rect.zw;
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
