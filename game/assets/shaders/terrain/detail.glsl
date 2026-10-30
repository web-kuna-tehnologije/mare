uniform sampler2D u_det; uniform sampler2D u_det_far;
uniform vec4 u_det_rect; uniform vec4 u_det_far_rect;
uniform vec2 u_det_on; uniform vec2 u_det_texel;
// on: near, far | texel: world units per texel
float det_fade(vec2 uv){
  if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 0.0;
  return clamp(min(min(uv.x, 1.0-uv.x), min(uv.y, 1.0-uv.y)) * 10.0, 0.0, 1.0); }
float det_at(vec2 xz){
  vec2 nuv = (xz - u_det_rect.xy) * u_det_rect.zw;
  vec2 fuv = (xz - u_det_far_rect.xy) * u_det_far_rect.zw;
  float nf = u_det_on.x > 0.5 ? det_fade(nuv) : 0.0;
  float ff = u_det_on.y > 0.5 ? det_fade(fuv) : 0.0;
  float dn = nf > 0.0 ? texture(u_det, nuv).r : 0.0;
  float df = ff > 0.0 ? texture(u_det_far, fuv).r * ff : 0.0;
  return mix(df, dn, nf); }
// world-space step for differentiating det_at: the resolution of whichever ring is in use
float det_step(vec2 xz){
  vec2 nuv = (xz - u_det_rect.xy) * u_det_rect.zw;
  return (u_det_on.x > 0.5 && det_fade(nuv) > 0.0) ? u_det_texel.x : u_det_texel.y; }
