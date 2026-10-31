// Map view modes (ui.h MAP_MODE_*, 0.22): one colouring of the ground per mode, laid over the
// painted world. Shared by the terrain and the sea pass; terrain.cc map_mode_bind sets these.
uniform int u_mapmode;                 // 0 terrain, 1 depth, 2 religion, 3 political, 4 influence, 5 production
uniform float u_nation_religion[11];   // index 0 unowned, then nation 0..9: 0 none, 1 Catholic, 2 Protestant, 3 Orthodox, 4 Islam
uniform vec3 u_good_colors[18];        // sRGB, index 0 = no good, then goods[]
uniform sampler2D u_reginfo;           // 256 x 1: R dominant good + 1, G development, B total potential
// the faith of unowned land, by where it is: a reading of the period's map
float mm_faith_geo(float lon, float lat) {
  if (lat < 36.5 && lon > -12.0 && lon < 60.0) return 4.0;                       // the Maghreb, Egypt, Arabia
  if (lat < 42.0 && lon > 26.0) return 4.0;                                      // Anatolia, the Levant
  if (lon > 19.0 && lat > 40.0 && lat < 56.0 && !(lon < 24.0 && lat > 46.0)) return 3.0;   // the Balkans, Muscovy
  if (lat > 52.0 && lon > -12.0 && lon < 32.0) return 2.0;                       // the North: Scandinavia, the Baltic, northern Germany, Britain
  if (lat > 48.5 && lon > 4.0 && lon < 16.0) return 2.0;
  if (lon < -30.0) return 0.0;                                                   // the New World: none yet
  return 1.0;                                                                    // Catholic Europe
}
vec3 mm_faith_color(float f) {
  if (f < 0.5) return vec3(0.45, 0.45, 0.45);
  if (f < 1.5) return vec3(0.93, 0.78, 0.30);    // Catholic: gold
  if (f < 2.5) return vec3(0.42, 0.40, 0.82);    // Protestant: violet
  if (f < 3.5) return vec3(0.80, 0.33, 0.20);    // Orthodox: red earth
  return vec3(0.20, 0.62, 0.32);                 // Islam: green
}
// a hypsometric tint for the depth mode: shallows pale, basins near black, with a contour
// every quarter unit so the shelf edge reads
vec3 mm_depth_color(float depth) {
  vec3 c = depth < 0.3 ? mix(vec3(0.80, 0.95, 0.95), vec3(0.45, 0.78, 0.90), depth / 0.3)
         : depth < 1.2 ? mix(vec3(0.45, 0.78, 0.90), vec3(0.10, 0.35, 0.70), (depth - 0.3) / 0.9)
         :               mix(vec3(0.10, 0.35, 0.70), vec3(0.02, 0.05, 0.18), clamp((depth - 1.2) / 3.5, 0.0, 1.0));
  float ct = abs(fract(depth / 0.25 + 0.5) - 0.5) * 0.25;
  return mix(c, c * 0.6, 1.0 - smoothstep(0.0, 0.012 + depth * 0.004, ct));
}
vec3 mm_height_color(float h) {
  return h < 0.6 ? mix(vec3(0.55, 0.72, 0.42), vec3(0.78, 0.74, 0.40), h / 0.6)
       : h < 3.0 ? mix(vec3(0.78, 0.74, 0.40), vec3(0.62, 0.42, 0.26), (h - 0.6) / 2.4)
       :           mix(vec3(0.62, 0.42, 0.26), vec3(0.96, 0.96, 0.98), clamp((h - 3.0) / 5.0, 0.0, 1.0));
}
// the colour of a land cell in the thematic modes; `tt` is the territory texel (nation+1,
// development, region+1, influence), `owner` the nation colour, `h` the height
vec3 mm_land_color(int mode, vec4 tt, vec3 owner, float h, float lon, float lat, vec3 parch) {
  int nid = int(tt.r * 255.0 + 0.5);
  if (mode == 1) return mm_height_color(h);
  if (mode == 2) { float f = nid > 0 ? u_nation_religion[min(nid, 10)] : mm_faith_geo(lon, lat); return mix(mm_faith_color(f), parch, nid > 0 ? 0.0 : 0.25); }
  if (mode == 3) return nid > 0 ? owner : parch * 0.9;
  if (mode == 4) { float k = tt.a; return nid > 0 ? mix(vec3(0.12, 0.10, 0.10), owner, 0.25 + 0.75 * k) : vec3(0.18, 0.17, 0.16); }
  if (mode == 5) {
    vec4 ri = texture(u_reginfo, vec2((tt.b * 255.0 + 0.5) / 256.0, 0.5));
    int g = int(ri.r * 255.0 + 0.5);
    if (g <= 0) return vec3(0.22, 0.22, 0.20);
    return mix(u_good_colors[min(g, 17)] * 0.45, u_good_colors[min(g, 17)], 0.3 + 0.7 * ri.g);
  }
  return parch;
}
