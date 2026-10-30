in vec2 v_uv; in vec4 v_col; in float v_shape; in float v_outline;
out vec4 fragColor;
#include "common/color.glsl"
void main(){
  vec2 p = v_uv; float d; int sh = int(v_shape + 0.5);
  if (sh == 0) d = length(p) - 0.8;
  else if (sh == 1) d = abs(length(p) - 0.7) - 0.14;
  else if (sh == 2) { vec2 q = vec2(abs(p.y), p.x); d = max(q.x*0.9 + q.y*0.5 - 0.45, -q.y - 0.5); }
  else if (sh == 3) d = abs(p.x) + abs(p.y) - 0.8;
  else if (sh == 4) d = max(abs(p.x), abs(p.y)) - 0.7;
  else if (sh == 5) { float a = atan(p.y, p.x); d = abs(length(p) - 0.6 - 0.15*sin(a*3.0)) - 0.12; }
  else { d = max(abs(p.x) - 0.12, abs(p.y) - 0.8); d = min(d, max(abs(p.x)-0.7, abs(p.y)-0.12)); }
  float aa = 0.08;
  float inside = 1.0 - smoothstep(-aa, aa, d);
  float outline = 1.0 - smoothstep(0.10, 0.10+aa, abs(d));
  vec3 col = mix(v_col.rgb, vec3(0.05), outline * v_outline);
  float alpha = max(inside, outline*v_outline) * v_col.a;
  if (alpha < 0.02) discard;
  fragColor = vec4(srgb_to_linear(col), alpha);
// icons are authored in sRGB; the HDR target is linear
}
