uniform mat4 u_vp; uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;
uniform float u_time; uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;
uniform vec4 u_ships[24]; uniform float u_shiplen[24]; uniform int u_nships; uniform float u_shipwave;
uniform float u_shallow;
// depth (in wave-scale units) the swell needs to reach full height: ~1.1 close in, up to ~3.6 at the strategic zoom, so the open sea runs and the coasts stay calm
uniform float u_mapness_vs;
// the map morph, for the detail rings below (the sea's own u_mapness is a fragment uniform)
// The wake map (gfx/wake.cc, 0.22.9): R foam, G the water a hull has already broken. The
// vertex stages read G - broken water lies flatter and carries its own short chop, which is
// what makes a trail read as *sea she has passed through* and not as paint on it.
uniform sampler2D u_wake; uniform vec4 u_wake_rect; uniform float u_has_wake;
// Muzzle blasts (0.22.9, issue6 "shock around ship in waves form"): (x, z, radius, strength)
// and (firing dir x, z, inner hole, ring width). A ring of ripples running out from the ship,
// heavier on the side she is firing and never directly under the guns.
uniform vec4 u_blast[12]; uniform vec4 u_blastdir[12]; uniform int u_nblast;
vec2 wake_at(vec2 p) {
  if (u_has_wake < 0.5) return vec2(0.0);
  vec2 uv = (p - u_wake_rect.xy) * u_wake_rect.zw;
  if (uv.x < 0.001 || uv.x > 0.999 || uv.y < 0.001 || uv.y > 0.999) return vec2(0.0);
  return textureLod(u_wake, uv, 0.0).rg;
}
// how hard the blasts are shaking the water here, and the ring's own ripple
float blast_lift(vec2 p, float t) {
  float h = 0.0;
  for (int i = 0; i < u_nblast; ++i) {
    vec2 d = p - u_blast[i].xy; float r = length(d); float R = u_blast[i].z;
    if (r > R * 1.6 + 2.0) continue;
    float w = max(u_blastdir[i].w, 0.05);
    float side = 0.35 + 0.65 * clamp(dot(normalize(d + vec2(1e-5)), u_blastdir[i].xy), 0.0, 1.0);
    float hole = smoothstep(u_blastdir[i].z * 0.6, u_blastdir[i].z, r);   // nothing right under the guns
    float ring = exp(-pow((r - R) / w, 2.0));
    h += u_blast[i].w * side * hole * ring * cos((r - R) / w * 2.2) * 0.085;   // 0.22.13 (issue10 "waves bigger"): 0.035 -> 0.085
  }
  return h;
}
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
    if (dot(dxz, dxz) > 20.0 * L * L) continue;   // 0.22.13 (issue10 "actual 3D wave"): the arms used to be cut at 1.55 L while their foam ran to 5 L; the geometry now runs out as far as the white does
    float spd = clamp(u_ships[i].w * 10.0, 0.0, 1.0);
    if (spd < 0.05) continue;
    float ch = cos(u_ships[i].z), sh = -sin(u_ships[i].z);
    float along = dxz.x * ch + dxz.y * sh, across = -dxz.x * sh + dxz.y * ch;
    vec2 q = vec2(along - 0.46 * L, across); float r = max(length(q), 1e-3);
    float ahead = smoothstep(-0.55, 0.35, q.x / r);
    float side = 1.0 - smoothstep(0.30 * L, 0.70 * L, abs(across));
    float env = exp(-r / (0.35 * L)) * ahead * side * (1.0 - smoothstep(0.9 * L, 1.3 * L, r));
    float ph = 6.28318 / (0.30 * L) * r - t * (2.0 + 4.0 * spd);
    hgt += 0.012 * L * spd * env * (0.7 * cos(ph) + 0.3);   // 0.22.5: a bigger bow wave; 0.22.12: smaller - the water goes into the arms below, not a ring ahead (issue9 "waves should be directional")
    float hull = smoothstep(-0.05 * L, 0.25 * L, along) * (1.0 - smoothstep(0.40 * L, 0.50 * L, along))
               * exp(-abs(abs(across) - 0.19 * L) / (0.08 * L));
    hgt += 0.012 * L * spd * hull;
// The trailing wake (0.22.6, issue3 "ship wakes ... in 3D with foaming"). A real hull leaves a
// Kelvin pattern: two diverging crests at about 19.5 degrees either side of the track, and the
// flat turbulent lane between them. Both are *geometry* here, not paint - the arms are a wave
// train whose phase runs along each arm, so the wake rolls outward as she goes, and the lane is
// a shallow trough. It decays over about six hull lengths, which is what keeps it from
// becoming the scar across the ocean the old V wake was (0.19).
    float astern = 0.46 * L - along;              // how far behind her stem (0.22.12: measured from the stem, so the arms root at the cutwater)
    if (astern > 0.0) {
      float fade = exp(-astern / (3.2 * L)) * spd * (1.0 - smoothstep(3.6 * L, 4.4 * L, length(dxz)));   // 0.22.13: eased to nothing before the early-out radius, so the mesh never steps
// 0.22.12 (issue9, wake.png): the diverging arms are the wake. They start at the stem (the
// bow wave *is* their root), run out at 19.5 degrees as a steep, breaking crest with a
// trough inboard of it, and carry a wave train along their length - twice the height they
// had, in a band half as wide, so the V is a pair of ridges on the sea and not a smear.
      float arm = abs(across) - astern * 0.354;   // distance from the 19.5-degree arm
      float armw = 0.10 * L + 0.035 * astern;     // the arm broadens as it runs out
      float armk = exp(-(arm * arm) / (armw * armw)) * fade;
      float aph = 6.28318 / (0.42 * L) * (astern * 1.06 + abs(across) * 0.35) - t * 1.4;
      hgt += 0.052 * L * armk * (0.55 + 0.45 * cos(aph));   // 0.22.13 (issue10, wake.png as a 3D wave): the V's crests half as high again
      hgt -= 0.018 * L * exp(-pow((arm + 0.9 * armw) / armw, 2.0)) * fade;   // the trough just inboard of the crest (0.22.13: deeper with the crest)
      float lane = (1.0 - smoothstep(0.0, 0.30 * L + 0.22 * astern, abs(across))) * fade;
      hgt -= 0.010 * L * lane;                    // the flattened, dragged-down lane astern
// the stern wave (0.22.11, issue8 "real generated waves from stern and bow"): the water closing
// behind the counter heaps into a transverse crest a tenth of a length astern, then runs out
// as a short train of transverse waves down the lane, a hull length apart, fading in three
      float sternw = (1.0 - smoothstep(0.18 * L, 0.34 * L, abs(across))) * spd;
      hgt += 0.022 * L * sternw * exp(-pow((astern - 0.12 * L) / (0.10 * L), 2.0));
      hgt += 0.012 * L * sternw * cos(6.28318 / (0.62 * L) * astern - t * 1.6) * exp(-astern / (1.6 * L)) * smoothstep(0.1 * L, 0.4 * L, astern);
    }
  }
// the trail she has already left (the wake map): the broken water lies a little lower than
// the swell around it and carries a short, steep chop of its own
  vec2 wk = wake_at(pp);
  if (wk.y > 0.002) {
    float L0 = u_wave_scale;
    hgt -= 0.010 * L0 * wk.y;
    hgt += 0.007 * L0 * wk.y * sin(pp.x * (11.0 / L0) + pp.y * (7.0 / L0) - t * 3.1);   // 0.22.13: the left trail keeps a visible chop
  }
  hgt += blast_lift(pp, t) * u_wave_scale;
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
// Staged shore profile (0.22.7, staged_scene.cc): a = shoreline point + unit normal pointing
// inland, b = wiggle amplitude, wiggle frequency, seabed slope, on. The staged scenes used to
// be a constant depth with no shore at all; now the depth runs down a beach profile off a
// wiggling line, negative (= land) inland, so the surf code and the sea's own discard apply.
uniform vec4 u_shore_a; uniform vec4 u_shore_b;
float local_depth_at(vec2 p) {
  if (u_shore_b.w < 0.5) return u_local_depth;
  vec2 d = p - u_shore_a.xy;
  float v = dot(d, u_shore_a.zw), u = dot(d, vec2(-u_shore_a.w, u_shore_a.z));
  float w = u_shore_b.x * (sin(u * u_shore_b.y) + 0.5 * sin(u * u_shore_b.y * 2.3 + 1.7) + 0.25 * sin(u * u_shore_b.y * 5.1 + 0.4));
  return clamp(-(v - w) * u_shore_b.z, -2.0, u_local_depth);
}
float height_at(vec2 p) {
  if (u_local_depth > 0.0) return -local_depth_at(p);
  float lon = p.x / u_k, lat = -p.y / u_k;
  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));
  float h = texture(u_hm, uv).r + det_at(p) * (1.0 - u_mapness_vs);
  return h;
}
