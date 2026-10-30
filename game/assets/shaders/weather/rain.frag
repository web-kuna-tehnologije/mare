// rain: three layers of streaks falling with the wind's slant, drawn over the finished frame
in vec2 v_uv; uniform float u_rain, u_time, u_aspect, u_slant; out vec4 fragColor;
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
void main(){
  if (u_rain <= 0.001) discard;
  vec2 p = vec2(v_uv.x * u_aspect, v_uv.y);
  float a = 0.0;
  for (int i = 0; i < 3; ++i) {
    float sc = 70.0 + 45.0 * float(i);
    vec2 q = p * sc; q.x += q.y * u_slant * 0.45; q.y += u_time * (16.0 + 7.0 * float(i));
    vec2 cell = floor(q); float h = hash(cell);
    float on = step(0.86 - 0.3 * u_rain, hash(cell + vec2(7.0, 3.0)));
    float fx = fract(q.x), fy = fract(q.y);
    float streak = smoothstep(0.09, 0.0, abs(fx - 0.5 - (h - 0.5) * 0.5)) * smoothstep(0.0, 0.25, fy) * smoothstep(1.0, 0.7, fy);
    a += streak * on * (0.32 - 0.07 * float(i));
  }
  fragColor = vec4(vec3(0.78, 0.82, 0.90), clamp(a, 0.0, 1.0) * u_rain * 0.6);
}
