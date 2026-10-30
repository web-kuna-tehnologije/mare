in vec2 v_uv; uniform float u_cover, u_time, u_aspect;
out vec4 fragColor;
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }
void main(){
  if (u_cover <= 0.001) discard;
  vec2 p = vec2(v_uv.x * u_aspect, v_uv.y) * 3.0 + vec2(u_time * 0.05, u_time * 0.013);
  float f = 0.0, a = 0.55; for (int i = 0; i < 5; ++i) { f += a * noise(p); p *= 2.03; a *= 0.5; }
  float edge = 1.05 - u_cover * 1.6;
// cover grows: threshold sweeps down through the fbm
  float alpha = smoothstep(edge, edge + 0.38, f) * smoothstep(0.0, 0.08, u_cover);
  vec3 col = mix(vec3(0.78, 0.82, 0.88), vec3(0.98, 0.985, 1.0), f);
  fragColor = vec4(col, alpha);
}
