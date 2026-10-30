// LOD cross-fade (gfx/lod.h, inst_t.dither): an ordered 4x4 Bayer threshold per pixel. d > 0
// discards the pixels whose threshold is below d (the level fading out keeps a 1-d share),
// d < 0 discards the complement (the level fading in keeps a -d share). Both levels of a fading
// instance therefore partition the screen exactly, in every pass that uses this (main, shadow).
// the 4x4 Bayer matrix as bit arithmetic (GLSL 1.30 has no const arrays worth trusting): 
// M2(x,y) = 2*((x^y)&1) + (y&1); M4 = 4*M2(x&1,y&1) + M2(x>>1,y>>1)
float dither_bayer4(vec2 p) {
  int x = int(mod(p.x, 4.0)), y = int(mod(p.y, 4.0));
  int m = 4 * (2 * ((x ^ y) & 1) + (y & 1)) + (2 * (((x >> 1) ^ (y >> 1)) & 1) + ((y >> 1) & 1));
  return (float(m) + 0.5) / 16.0;
}
void dither_clip(float d) {
  if (d == 0.0) return;
  float b = dither_bayer4(gl_FragCoord.xy);
  if (d > 0.0 ? b < d : b >= -d) discard;
}
