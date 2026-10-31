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
// The rings' shore masks (gfx/streamview.cc, 0.22): R cliff, G beach, B marsh, A exposure -
// what kind of coast a texel is, zero everywhere the shore profile left the ground alone.
// Read from whichever ring answers, like det_at.
uniform sampler2D u_shore; uniform sampler2D u_shore_far;
vec4 shore_at(vec2 xz){
  vec2 nuv = (xz - u_det_rect.xy) * u_det_rect.zw;
  vec2 fuv = (xz - u_det_far_rect.xy) * u_det_far_rect.zw;
  float nf = u_det_on.x > 0.5 ? det_fade(nuv) : 0.0;
  float ff = u_det_on.y > 0.5 ? det_fade(fuv) : 0.0;
  vec4 sn = nf > 0.0 ? texture(u_shore, nuv) : vec4(0.0);
  vec4 sf = ff > 0.0 ? texture(u_shore_far, fuv) * ff : vec4(0.0);
  return mix(sf, sn, nf); }

// Terrain sun occlusion (0.22, "light occlusion"): is the sun above the ground's horizon seen
// from this point? Ten taps march towards the sun through the base heightmap plus the rings,
// the step growing 1.6x each time (0.6 .. 40 world units), and the biggest rise of the ground
// above the ray - as a tangent, so a near ridge and a far mountain weigh alike - becomes a soft
// shadow factor. Mountains shade their valleys and the sea at their foot, which the shadow
// atlas (meshes only) never did. Zero cost on the map: the morph fades it out.
float det_sun_occ(vec3 wpos, vec3 sun, sampler2D hm, vec4 geo, float k, float mapness) {
  if (sun.y < 0.015 || mapness > 0.95) return 1.0;
  float occ = 0.0, t = 0.6;
  for (int i = 0; i < 10; ++i) {
    vec3 p = wpos + sun * t;
    float lon = p.x / k, lat = -p.z / k;
    vec2 uv = vec2((lon - geo.x) / (geo.y - geo.x), (geo.w - lat) / (geo.w - geo.z));
    float h = texture(hm, uv).r + det_at(p.xz) * (1.0 - mapness);
    occ = max(occ, (h - p.y) / (t * 0.6 + 0.25));
    t *= 1.6;
  }
  return 1.0 - smoothstep(0.0, 0.35, occ) * 0.92 * (1.0 - mapness);
}

// The rings' river textures (gfx/streamview.cc, 0.22): R water, G/B flow direction (0.5 = 0),
// A width - where a river runs and which way. Read like shore_at.
uniform sampler2D u_river; uniform sampler2D u_river_far;
vec4 river_at(vec2 xz){
  vec2 nuv = (xz - u_det_rect.xy) * u_det_rect.zw;
  vec2 fuv = (xz - u_det_far_rect.xy) * u_det_far_rect.zw;
  float nf = u_det_on.x > 0.5 ? det_fade(nuv) : 0.0;
  float ff = u_det_on.y > 0.5 ? det_fade(fuv) : 0.0;
  vec4 rn = nf > 0.0 ? texture(u_river, nuv) : vec4(0.0, 0.5, 0.5, 0.0);
  vec4 rf = ff > 0.0 ? texture(u_river_far, fuv) : vec4(0.0, 0.5, 0.5, 0.0);
  rf.r *= ff;
  return mix(rf, rn, nf); }
