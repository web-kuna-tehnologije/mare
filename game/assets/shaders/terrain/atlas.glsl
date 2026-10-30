// The resident fine terrain tiles, sampled at their own resolution (gfx/heightatlas.h).
//
// The clipmap ring carries one sample per ~7 of the fine set's, so everything finer than a
// ring texel is lost before the vertex stage. ha_detail() puts exactly that back: it samples
// the atlas at the point, reconstructs what the ring holds there - the same field point
// sampled on the ring grid and bilinearly interpolated, which is what the ring *is* - and
// returns the difference. The two layers therefore sum to the data once, the residual goes to
// zero wherever the ring already resolves the ground, and nothing is added at all where no
// tile is resident.
uniform sampler2D u_hatlas;   // R32F, world-unit heights, HA_COLS x HA_ROWS slots
uniform sampler2D u_hslots;   // tile grid -> slot + 1 (0 = not resident), NEAREST
uniform vec4 u_ha_geo;        // lon0, lat1, 1/lon span, 1/lat span of the fine set
uniform vec4 u_ha_grid;       // tiles_x, tiles_y, samples across, samples down
uniform vec4 u_ha_slot;       // cols, rows, texels per slot, 1/atlas edge
uniform vec2 u_ha_on;         // x = the atlas holds something, y = ring texel in world units
uniform float u_ha_k;         // world units per degree

float ha_at(vec2 xz, out bool ok) {
  ok = false;
  if (u_ha_on.x < 0.5) return 0.0;
  float lon = xz.x / u_ha_k, lat = -xz.y / u_ha_k;
  vec2 g = vec2((lon - u_ha_geo.x) * u_ha_geo.z, (u_ha_geo.y - lat) * u_ha_geo.w);
  if (g.x < 0.0 || g.x > 1.0 || g.y < 0.0 || g.y > 1.0) return 0.0;
  vec2 smp = g * vec2(u_ha_grid.z - 1.0, u_ha_grid.w - 1.0);
  vec2 tile = floor(smp / u_ha_slot.z);
  if (tile.x < 0.0 || tile.y < 0.0 || tile.x >= u_ha_grid.x || tile.y >= u_ha_grid.y) return 0.0;
  float s = texture(u_hslots, (tile + 0.5) / u_ha_grid.xy).r - 1.0;
  if (s < -0.5) return 0.0;
// half a texel in from the slot's edge: a bilinear tap must not reach into its neighbour
  vec2 local = clamp(smp - tile * u_ha_slot.z, 0.5, u_ha_slot.z - 0.5);
  vec2 cell = vec2(mod(s, u_ha_slot.x), floor(s / u_ha_slot.x));
  ok = true;
  return texture(u_hatlas, (cell * u_ha_slot.z + local) * u_ha_slot.w).r;
}

float ha_detail(vec2 xz) {
  if (u_ha_on.x < 0.5 || u_ha_on.y <= 0.0) return 0.0;
  bool ok; float full = ha_at(xz, ok);
  if (!ok) return 0.0;
  float t = u_ha_on.y;
  vec2 b = floor(xz / t) * t, f = (xz - b) / t;
  bool o0, o1, o2, o3;
  float h00 = ha_at(b, o0);
  float h10 = ha_at(b + vec2(t, 0.0), o1);
  float h01 = ha_at(b + vec2(0.0, t), o2);
  float h11 = ha_at(b + vec2(t, t), o3);
// at the edge of the resident set one corner of the ring cell can fall outside: contribute
// nothing there rather than subtracting a reconstruction that is missing a sample
  if (!(o0 && o1 && o2 && o3)) return 0.0;
  return full - mix(mix(h00, h10, f.x), mix(h01, h11, f.x), f.y);
}

// Detail applied to a composed height, without letting it cross the waterline.
//
// The residual is zero-mean against the ring, but near a steep shore its local swing is
// easily a whole world unit - more than enough to lift a shallow seabed into land or drop a
// beach under water. The coastline is not ours to move: the ports, the harbour basins and the
// navigation grid were all placed against the base map's shore (world/world.h,
// world/terrainmap.h). So the detail shapes the ground on whichever side of the water it is
// already on, and stops at the line. `k` is the map-flatten weight, 0 in the parchment view.
float ha_shape(float h, vec2 xz, float k) {
  if (k <= 0.0) return h;
  float d = ha_detail(xz) * k;
  if (d == 0.0) return h;
  if (h >  0.02) return max(h + d,  0.02);
  if (h < -0.02) return min(h + d, -0.02);
  return h;
}
