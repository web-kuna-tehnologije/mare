// The wake map's fade and spread (gfx/wake.cc, 0.22.11). Reads the last frame's map and writes
// this frame's: a small cross blur (u_texel is the spread this frame) times the decay factor.
// The blur is the cone - churn leaks outward a fraction of a texel every frame, so an old
// trail is wide and faint and a fresh one narrow and bright - and the decay is the fading.
in vec2 v_uv; out vec4 fragColor;
uniform sampler2D u_src; uniform vec2 u_texel; uniform float u_fade;
void main(){
  vec4 c = texture(u_src, v_uv) * 0.52;
  c += texture(u_src, v_uv + vec2(u_texel.x, 0.0)) * 0.12 + texture(u_src, v_uv - vec2(u_texel.x, 0.0)) * 0.12;
  c += texture(u_src, v_uv + vec2(0.0, u_texel.y)) * 0.12 + texture(u_src, v_uv - vec2(0.0, u_texel.y)) * 0.12;
  fragColor = c * u_fade;
}
