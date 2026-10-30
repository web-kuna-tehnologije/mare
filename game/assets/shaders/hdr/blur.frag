in vec2 v_uv; out vec4 fragColor;
uniform sampler2D u_src; uniform vec2 u_dir;
// u_dir: one texel along the blur axis
void main(){
  const float w[5] = float[5](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);
  vec3 c = texture(u_src, v_uv).rgb * w[0];
  for (int i = 1; i < 5; ++i) { vec2 o = u_dir * float(i) * 1.3; c += (texture(u_src, v_uv + o).rgb + texture(u_src, v_uv - o).rgb) * w[i]; }
  fragColor = vec4(c, 1.0);
}
