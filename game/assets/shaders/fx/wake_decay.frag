// The wake map's fade and spread (gfx/wake.cc, 0.22.11). Reads the last frame's map and writes
// this frame's: a small cross blur (u_texel is the spread this frame) times the decay factor.
// The blur is the cone - churn leaks outward a fraction of a texel every frame, so an old
// trail is wide and faint and a fresh one narrow and bright - and the decay is the fading.
in vec2 v_uv; out vec4 fragColor;
uniform sampler2D u_src; uniform vec2 u_texel; uniform float u_fade;
void main(){
// 0.22.13: a wider cross (two texel rings) so the spread is a real diffusion, and the churn
// (G) outlives the white (R) a little less: the foam goes first, the disturbed water after
  vec4 c = texture(u_src, v_uv) * 0.40;
  c += (texture(u_src, v_uv + vec2(u_texel.x, 0.0)) + texture(u_src, v_uv - vec2(u_texel.x, 0.0))
      + texture(u_src, v_uv + vec2(0.0, u_texel.y)) + texture(u_src, v_uv - vec2(0.0, u_texel.y))) * 0.10;
  c += (texture(u_src, v_uv + vec2(u_texel.x, u_texel.y)) + texture(u_src, v_uv - vec2(u_texel.x, u_texel.y))
      + texture(u_src, v_uv + vec2(u_texel.x, -u_texel.y)) + texture(u_src, v_uv - vec2(u_texel.x, -u_texel.y))) * 0.05;
  fragColor = vec4(c.r * u_fade * u_fade, c.g * u_fade, c.ba * u_fade);
}
