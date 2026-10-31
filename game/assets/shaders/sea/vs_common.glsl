uniform mat4 u_vp; uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;
uniform float u_time; uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform vec4 u_ships[24]; uniform float u_shiplen[24]; uniform int u_nships; uniform float u_shipwave;
uniform float u_shallow;
// depth (in wave-scale units) the swell needs to reach full height: ~1.1 close in, up to ~3.6 at the strategic zoom, so the open sea runs and the coasts stay calm
uniform float u_mapness_vs;
// the map morph, for the detail rings below (the sea's own u_mapness is a fragment uniform)
#include "terrain/rings.glsl"
out vec3 v_wpos; out vec3 v_nrm; out float v_slope; out float v_h; out float v_amp; out float v_edge; out float v_jac; out float v_crest; out float v_shipw;
// The water a ship pushes aside. Each nearby ship (x, z, heading, speed in hull lengths per 
// second; hull length) raises a bow wave: a crest heaped at the stem that rings outward 
// *ahead* of her and round the shoulders, decaying within a hull length, plus the pile-up 
// along the forward half of the hull. Nothing astern - at this scale a wake reads as a 
// scar on the sea, and what the eye wants is the water in front of a moving bow. Height is 
// v^2/2g in spirit: it grows with speed and with the hull.
float ship_wave(vec2 pp, float t) {
  float hgt = 0.0;
  if (u_shipwave < 0.5) return 0.0;
  for (int i = 0; i < u_nships; ++i) {
    vec2 dxz = pp - u_ships[i].xy; float L = max(u_shiplen[i], 0.2);
    if (dot(dxz, dxz) > 2.4 * L * L) continue;
    float spd = clamp(u_ships[i].w * 10.0, 0.0, 1.0);
    if (spd < 0.05) continue;
    float ch = cos(u_ships[i].z), sh = -sin(u_ships[i].z);
    float along = dxz.x * ch + dxz.y * sh, across = -dxz.x * sh + dxz.y * ch;
    vec2 q = vec2(along - 0.46 * L, across); float r = max(length(q), 1e-3);
    float ahead = smoothstep(-0.55, 0.35, q.x / r);
    float side = 1.0 - smoothstep(0.30 * L, 0.70 * L, abs(across));
    float env = exp(-r / (0.35 * L)) * ahead * side * (1.0 - smoothstep(0.9 * L, 1.3 * L, r));
    float ph = 6.28318 / (0.30 * L) * r - t * (2.0 + 4.0 * spd);
    hgt += 0.013 * L * spd * env * (0.7 * cos(ph) + 0.3);
    float hull = smoothstep(-0.05 * L, 0.25 * L, along) * (1.0 - smoothstep(0.40 * L, 0.50 * L, along))
               * exp(-abs(abs(across) - 0.19 * L) / (0.08 * L));
    hgt += 0.008 * L * spd * hull;
  }
  return hgt;
}
// shoaling: a wave entering water shallower than its length steepens before it breaks. The 
// boost peaks a little off the beach; the shallows fade still flattens the last metres so 
// nothing breaks through the sand.
float shoal_boost(float dep, float scale) { float x = (dep - 0.95 * scale) / (0.5 * scale); return 1.0 + 0.7 * exp(-x * x); }
// raw heightmap value: negative below sea level; > 0.35 is land, which the sea discards.
// The streamed detail rings (terrain/rings.glsl) are added exactly as the terrain adds
// them, so the water's edge follows the *detailed* shore - the authored coastline near the
// camera - and not the base raster's cell steps (0.19 coast polish).
float height_at(vec2 p) {
  if (u_local_depth > 0.0) return -u_local_depth;
  float lon = p.x / u_k, lat = -p.y / u_k;
  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));
  float h = texture(u_hm, uv).r + det_at(p) * (1.0 - u_mapness_vs);
  return h;
}
