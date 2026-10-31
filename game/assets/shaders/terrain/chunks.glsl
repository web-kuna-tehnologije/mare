// The terrain-v2 chunks' material maps (gfx/terrain_atlas.h): biome class, splat weights and
// the terrain-scale normal, per sample, for the dataset the atlas holds.
//
// Addressing follows assets/data_new/MARE_INTEGRATION.md: every map is *vertex-aligned*, so the
// sample at global column i sits at texel centre (i + 0.5) / 513 inside its chunk's slot; the
// class map is read nearest, the rest bilinear, no mipmaps. Chunks share their edge samples,
// so a point exactly on a chunk border reads the same value from either side.
uniform sampler2D u_ccls;     // R = biome id, G = coverage, B = provenance   (NEAREST)
uniform sampler2D u_csplat;   // grass, rock, sand, snow weights                (LINEAR)
uniform sampler2D u_cnrm;     // east / up / south normal, true-metre slopes    (LINEAR)
uniform sampler2D u_cslots;   // chunk grid -> slot + 1 (0 = not resident)      (NEAREST)
uniform vec4 u_cgeo;          // west, north, 1/lon span, 1/lat span
uniform vec4 u_cgrid;         // chunks_x, chunks_y, cells per chunk (512), wraps
uniform vec4 u_cslot;         // cols, rows, texels per slot (513), 1/atlas edge
uniform vec2 u_con;           // x = the atlas holds something, y = vertical exaggeration of the drawn world

// atlas texture coordinates of a geographic point (texel-centre space, bilinear-safe), and
// the local sample position inside the chunk; false where no chunk is resident
bool ch_uv(float lon, float lat, out vec2 uv, out vec2 local, out vec2 cell) {
  uv = vec2(0.0); local = vec2(0.0); cell = vec2(0.0);
  if (u_con.x < 0.5) return false;
  float span = 1.0 / u_cgeo.z;
  float x = lon - u_cgeo.x;
  if (u_cgrid.w > 0.5) x -= span * floor(x / span); else if (x < 0.0 || x > span) return false;
  float y = u_cgeo.y - lat;
  if (y < 0.0 || y > 1.0 / u_cgeo.w) return false;
  vec2 gs = vec2(x * u_cgeo.z * u_cgrid.x, y * u_cgeo.w * u_cgrid.y) * u_cgrid.z;   // global sample coordinates
  vec2 ch = min(floor(gs / u_cgrid.z), u_cgrid.xy - 1.0);
  float s = texture(u_cslots, (ch + 0.5) / u_cgrid.xy).r * 255.0 - 1.0;
  if (s < -0.5) return false;
  local = clamp(gs - ch * u_cgrid.z, 0.0, u_cgrid.z - 0.001);     // 0 .. 512 (the shared edge sample)
  cell = vec2(mod(s, u_cslot.x), floor(s / u_cslot.x));
  uv = (cell * u_cslot.z + local + 0.5) * u_cslot.w;
  return true;
}

// The class at the four samples around the point and their bilinear weights, so a material
// mask can be built per class (ch_wclass) and classes meet along a slope between samples
// instead of a 3 km step. `cover` is the bilinear coverage (0 = unknown ground somewhere).
void ch_classes(vec2 local, vec2 cell, out vec4 ids, out vec4 wts, out float cover) {
  vec2 b = floor(local), f = local - b;
  vec2 o = cell * u_cslot.z + b + 0.5;
  vec4 c00 = texture(u_ccls, o * u_cslot.w);
  vec4 c10 = texture(u_ccls, (o + vec2(1.0, 0.0)) * u_cslot.w);
  vec4 c01 = texture(u_ccls, (o + vec2(0.0, 1.0)) * u_cslot.w);
  vec4 c11 = texture(u_ccls, (o + vec2(1.0, 1.0)) * u_cslot.w);
  ids = vec4(c00.r, c10.r, c01.r, c11.r) * 255.0;
  wts = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
  cover = dot(wts, vec4(c00.g, c10.g, c01.g, c11.g));
}
// the weight of one class (id) in that neighbourhood, 0..1
float ch_wclass(vec4 ids, vec4 wts, float id) {
  return dot(wts, vec4(lessThan(abs(ids - id), vec4(0.5))));
}
// grass / rock / sand / snow, normalised; all zero on water and missing ground
vec4 ch_splat(vec2 uv) {
  vec4 w = texture(u_csplat, uv);
  float s = w.r + w.g + w.b + w.a;
  return s > 0.004 ? w / s : vec4(0.0);
}
// the terrain-scale normal in world axes (x east, y up, z south), its slopes scaled to the
// world's vertical exaggeration; up when there is no data
vec3 ch_normal(vec2 uv) {
  vec3 n = texture(u_cnrm, uv).rgb * 2.0 - 1.0;
  if (dot(n, n) < 0.25) return vec3(0.0, 1.0, 0.0);
  n.xz *= u_con.y;
  return normalize(n);
}
