in vec3 v_wpos; in vec3 v_nrm; in float v_slope; in float v_h; in float v_amp; in float v_edge; in float v_jac; in float v_crest; in float v_shipw;
uniform sampler2D u_waves; uniform sampler2D u_albedo; uniform float u_has_albedo; uniform sampler2D u_hm;
uniform float u_foam;
// foam strength knob (dashboard / MARE_FOAM), 1 = normal
uniform float u_calm;
// weather: 1 = glassy (mirror-like roughness, more sky in the water)
uniform sampler2D u_scene; uniform sampler2D u_scene_depth; uniform vec2 u_scene_size; uniform float u_refract;
// the frame so far, for refraction (hdr_capture_scene); u_refract 0 = alpha blend instead
uniform vec3 u_cam_right; uniform vec3 u_cam_up;
uniform vec4 u_ships[24]; uniform float u_shiplen[24]; uniform int u_nships;
// nearby ships: x, z, heading, speed (hull lengths/s); hull length
uniform float u_time; uniform float u_wave_scale; uniform float u_local_depth; uniform vec4 u_geo; uniform float u_k;
uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_daylight;
uniform vec2 u_drift1; uniform vec2 u_drift2;
// accumulated downwind drift of the two ripple layers (sea_update_waves)
uniform vec4 u_hole;
// xy = centre, z = half size, w = 1 when the world pass must cut the detail square out
uniform vec2 u_clip;
// x = world z the water stops at, y = 1 when that clip is on (staged city: the shore)
uniform float u_sea_debug;
uniform float u_detail_far;
uniform float u_capzoom;
uniform float u_shorescale;
uniform vec2 u_zrange;
// the camera's near and far planes, to linearise the captured depth (refraction thickness, 0.22.4)
float lin_depth(float zw, vec2 nf) { float ndc = zw * 2.0 - 1.0; return 2.0 * nf.x * nf.y / (nf.y + nf.x - ndc * (nf.y - nf.x)); }
// the shore bands' width in wave units follows the zoom (sea.cc, 0.22.4): the lapping line, the
// surf and the cliff burst are a few hundred metres wide from the regional view and a few tens
// of metres from the battle zoom - fixed world widths made every close shore a sheet of milk
// 1 at the battle zoom, 0 from the regional view: whitecaps and foam streaks only where a wave is a wave on screen (sea.cc, 0.22.3)
// how far the ripple detail, glitter and foam keep their strength: scales with the camera (sea.cc)
// MARE_SEA_DEBUG=1: red = depth/2, green = land reading, blue = refraction on
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/nmap.glsl"
#include "common/fog.glsl"
#include "terrain/rings.glsl"
#include "common/mapmode.glsl"
uniform sampler2D u_terr; uniform vec2 u_terr_texel;
float shash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float snoise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(shash(i),shash(i+vec2(1,0)),f.x), mix(shash(i+vec2(0,1)),shash(i+vec2(1,1)),f.x), f.y); }
float depth_at(vec2 pp) {
// water depth from the heightmap plus the streamed detail rings (staged scenes: constant)
  if (u_local_depth > 0.0) return u_local_depth;
  float lon = pp.x / u_k, lat = -pp.y / u_k;
  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));
  float h = texture(u_hm, uv).r + det_at(pp) * (1.0 - u_mapness);
  return max(-h, 0.0);
}
void main(){
  if (u_hole.w > 0.5) {
// the world grid gives way to the detail region: a square for the warped grid, a disc for the patches
    if (u_hole.w > 1.5) { if (distance(v_wpos.xz, u_hole.xy) < u_hole.z) discard; }
    else if (abs(v_wpos.x - u_hole.x) < u_hole.z && abs(v_wpos.z - u_hole.y) < u_hole.z) discard;
  }
  if (u_local_depth <= 0.0 && v_h > 0.35) discard;
// land (the same threshold the sea has always used)
  if (u_clip.y > 0.5 && v_wpos.z > u_clip.x) discard;
// staged scenes have no heightmap: the shore is a line
  float lon = v_wpos.x / u_k, lat = -v_wpos.z / u_k;
  float depth = max(-v_h, 0.0);
  float dist = distance(v_wpos, u_campos);
// Two reaches (0.22). The ripple normal maps and the foam textures keep their old reach -
// at the regional zoom a ripple is well under a pixel and the maps alias into streaks - but
// the sun's glitter and the caustics follow the camera (u_detail_far), so the sea is still lit
// by the sun from far above instead of going flat past a fixed line.
  float detail = (1.0 - smoothstep(60.0 * u_wave_scale, 400.0 * u_wave_scale, dist)) * v_edge;
  float dfar = max(400.0 * u_wave_scale, u_detail_far);
  float detail_lit = (1.0 - smoothstep(0.15 * dfar, dfar, dist)) * v_edge;
// the sun's shadow on the water from the land: a cliff coast or a mountain range to the
// south-west puts the bay at its foot in shade (terrain/rings.glsl det_sun_occ)
  if (u_local_depth <= 0.0) { float ok = 1.0 - smoothstep(500.0, 900.0, dist);
    if (ok > 0.01) g_sun_occ = mix(1.0, max(det_sun_occ(v_wpos, u_sun, u_hm, u_geo, u_k, u_mapness), 0.55), ok); }
  vec2 p = v_wpos.xz / u_wave_scale; float t = u_time;
// two scrolling samples of the tileable wave normal map, large swell + fine chop, riding on
// top of the Gerstner normal (which carries the shape, this carries the ripple)
// the ripple detail drifts downwind, like the swell it sits on
  vec3 n1 = nmap_decode(texture(u_waves, p * 0.33 + u_drift1).rgb);
  vec3 n2 = nmap_decode(texture(u_waves, p * 1.15 + u_drift2 + vec2(0.37, 0.11)).rgb);
  vec2 slope = (n1.xy / max(n1.z, 0.3) + n2.xy / max(n2.z, 0.3) * 0.6) * 0.42 * detail;
  vec3 wn = normalize(v_nrm);
  vec3 n = normalize(wn + vec3(slope.x, 0.0, slope.y));
  float w1 = sin(p.x*3.1 + p.y*1.3 + t*1.7);
// keeps the foam band animated
// The body colour (0.22.3): blue-grey everywhere, the Adriatic included. The 0.22 "warm sea"
// ramp ran the shallows through aqua and turquoise, which is the colour of a lagoon over
// white sand, not of the sea along a limestone coast; what makes a shallow read as shallow is
// the bottom showing *through* it, so the shallows are left transparent (the absorption and
// alpha below) and the water itself only goes from a pale slate at the strand to a deep
// blue-grey past the shelf.
  vec3 deep = vec3(0.030,0.068,0.118), shallow = vec3(0.20,0.27,0.31);
  vec3 bodyc = mix(shallow, deep, 1.0 - exp(-depth*1.1));
// Abyssal darkening: the old ramp saturated at `deep` by ~3 units, so a shelf edge and an
// ocean basin read the same. The real bathymetry (GEBCO tiles) hangs the basins 2.5-4 units
// down; past the shelf the body keeps losing light exponentially towards an abyssal
// near-black, which is what draws the deep-water gradient over a trench.
  bodyc *= mix(1.0, 0.28, 1.0 - exp(-max(depth - 1.2, 0.0) * 0.55));
  vec3 c = srgb_to_linear(bodyc);
// --- refraction (step 5 follow-up). The scene behind the surface, sampled where the
// surface normal bends the view; what comes back is absorbed by the water column (red
// first), so past a few units of depth the seabed - and whether a terrain chunk was drawn
// there at all - no longer shows. Samples that land on something in front of the water
// (a hull) fall back to the straight-down sample.
  vec3 alb1 = texture(u_albedo, p * 0.33 + u_drift1).rgb;
  vec3 alb2 = texture(u_albedo, p * 1.15 + u_drift2 + vec2(0.37, 0.11)).rgb;
  float lum1 = dot(alb1, vec3(0.3, 0.5, 0.2)), lum2 = dot(alb2, vec3(0.3, 0.5, 0.2));
// 0.22.4: the Meshy foam tile is bright (median 0.29, top decile 0.81 against the old tile's
// flat 0.2), so its luminance is cubed before the thresholds below: foam stays in the top
// tenth of the texture instead of covering the sea
  lum1 = lum1 * lum1 * lum1; lum2 = lum2 * lum2 * lum2;
  vec3 vdir0 = normalize(u_campos - v_wpos);
  vec3 body0 = sun_shade(c, normalize(wn + vec3(slope.x, 0.0, slope.y)), vdir0, v_wpos, 0.9, 0.0, dist);
// the lit water column: what the refraction fades *into*
  vec3 refr = c; float refr_on = 0.0;
  if (u_refract > 0.5 && u_mapness < 0.99) {
    vec2 suv = gl_FragCoord.xy / u_scene_size;
    vec3 nn = normalize(v_nrm + vec3(slope.x, 0.0, slope.y) * 1.5);
    vec2 bend = vec2(dot(nn, u_cam_right), dot(nn, u_cam_up)) * 0.055 * detail / (1.0 + dist * 0.02);
    vec2 ruv = clamp(suv + bend, vec2(0.002), vec2(0.998));
    float dz = texture(u_scene_depth, ruv).r;
    if (dz < gl_FragCoord.z - 1e-5) { ruv = suv; dz = texture(u_scene_depth, ruv).r; }
    vec3 bg = texture(u_scene, ruv).rgb;
// Where no seabed was drawn (terrain chunks with no land are skipped) the frame holds the
// clear colour, which is sky, not sea floor: the water column's own colour stands in. This
// is what used to show as a straight-edged lighter wedge over deep water.
    if (dz >= 0.99999) bg = c;
// Absorption with green nearer red than blue: what comes back reads grey-blue rather than
// teal. The shallower coefficient also lets the bottom show through further out - the
// transparency the water was missing.
// Absorption (0.17: stronger). The shelf the heightfield now carries is shallow enough that
// the old coefficients showed the sand through almost unfiltered; at a unit of depth the
// floor should be a turquoise suggestion, not the ground plane.
// 0.22.3: clearer again. Blue-grey water over a pale bottom: the sand shows plainly in the
// first quarter unit, is a grey suggestion at half a unit and gone by one, and the red
// still goes first so the bottom cools with depth instead of staying tan.
    vec3 absorb = vec3(5.0, 3.5, 2.5) / u_wave_scale;
// The column the light actually crosses (0.22.4): the distance from the surface to whatever
// the refracted sample hit, from the captured depth, so a hull a metre under the waterline
// shows through while the seabed (or a staged scene's ground, forty units down) is absorbed.
// The vertical depth stays the ceiling, so the map's shallows keep their calibration.
    float thick = max(lin_depth(dz, u_zrange) - lin_depth(gl_FragCoord.z, u_zrange), 0.0) * max(abs(vdir0.y), 0.25);
    float adepth = min(depth, thick * 0.9 + 0.02 * u_wave_scale);
    vec3 tr = exp(-absorb * adepth * 1.05);
// Mix towards the *lit* water column, not the raw body colour: the sunlit seabed is twenty
// times brighter than an unlit blue, and won every shallow pixel however small `tr` was.
// Caustics (0.22): the surface focuses the sun on the shallow bottom - the two ripple layers
// bright crossings, sharpened, scrolling with them, fading out where the bottom is too deep
// to see and when the sun is low or the land shades the water.
// Caustics: the surface focuses the sun on a bottom shallow enough to see. Clamped hard - the
// first cut multiplied the seabed by up to 3.4 and turned every shelf into white bands (0.22).
    float cau = pow(clamp(lum1 * 0.6 + lum2 * 0.7, 0.0, 1.0), 3.0);
    float caufade = smoothstep(1.2, 0.2, depth) * smoothstep(0.02, 0.18, depth)
                  * clamp(u_sun.y * 2.0, 0.0, 1.0) * g_sun_occ * detail_lit;
    bg *= 1.0 + clamp(cau * caufade, 0.0, 0.45);
    refr = mix(body0, bg, tr); refr_on = 1.0;
  }
// authored surface: the foam albedo rides the swell layer (and a finer copy the chop), its
// bright streaks become whitecaps over the depth colour and its tonal variation breaks up
// the flat gradient; everything fades out with `detail` like the normals do
  float streak = max(smoothstep(0.22, 0.58, lum1), 0.5 * smoothstep(0.24, 0.62, lum2)) * u_has_albedo * detail * u_capzoom;
  c *= mix(1.0, clamp(0.80 + lum1 * 3.0, 0.7, 1.35), 0.30 * u_has_albedo * detail);
  vec3 vdir = normalize(u_campos - v_wpos);
// water: smooth dielectric, so the sun glint comes from the GGX lobe and the sky from Fresnel
// Specular antialiasing. One pixel covers many waves once the camera pulls back, and a
// sharp GGX lobe sampled at one point per pixel then turns into a field of fireflies.
// Measuring how much the normal actually swings per pixel and folding that variance into
// roughness fixes it at every distance, instead of guessing with a distance curve.
// Water is a dielectric: almost nothing diffuse. Its colour is what the column transmits
// (refr) plus the sky it reflects, weighted by Fresnel; the sun adds a glint. The old path
// lit the body colour as a diffuse surface and read as plastic.
  float rough = mix(0.5, 0.26, detail) * mix(1.0, 0.55, u_calm);
// calm air leaves the surface glassy
  vec3 ndx = dFdx(n), ndy = dFdy(n);
  float nvar = dot(ndx, ndx) + dot(ndy, ndy);
  rough = clamp(sqrt(rough*rough + nvar * 2.0), 0.0, 1.0);
// Fresnel from the wave normal with only a touch of the ripple detail: per-ripple Fresnel
// at a grazing angle turns the whole sea into sky-coloured chalk. Capped so the body colour
// still reads at water level, where a real sea is dark glass, not a mirror of the haze.
  vec3 nf = normalize(wn + vec3(slope.x, 0.0, slope.y) * 0.25);
  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(nf, vdir), 0.0), 5.0);
  fres = min(fres, 0.58 + 0.2 * u_calm + 0.17 * step(0.01, u_local_depth));   // staged scenes (battle, harbour) reflect more of the sky and the ships' light (0.22.4)
// Dispersion: the three channels leave the surface at slightly different angles, so a
// grazing reflection is faintly cool at the edge and warm inside it. A single achromatic
// lobe is what makes water read as moulded plastic.
  vec3 fres3 = clamp(vec3(fres) * vec3(0.93, 1.0, 1.09), 0.0, 1.0);
  vec3 skyrefl = srgb_to_linear(u_sky) * (0.40 + 0.4 * u_calm) + u_ambsky * 0.32;
  vec3 glint = sun_shade(vec3(0.0), n, vdir, v_wpos, rough, 0.0, dist) * 0.26;
// albedo 0: the specular lobe alone, in the sun's shadow where it should be
// Sparkle: the sub-ripple normals the shading normal has already averaged away, put back as
// a narrow glitter. A real sea's highlight is thousands of points, not one smooth patch.
  vec3 nsp = normalize(wn + vec3(slope.x, 0.0, slope.y) * 2.6);
  float sp = pow(max(dot(nsp, normalize(u_sun + vdir)), 0.0), 190.0);
  glint += u_suncol * sp * 0.22 * max(detail, 0.45 * detail_lit) * (1.0 - 0.6 * u_calm);
  vec3 body = sun_shade(c, n, vdir, v_wpos, 0.9, 0.0, dist);
// the little diffuse the suspended sediment gives
  vec3 col3d = mix(mix(body, refr, refr_on), skyrefl, fres3) + glint;
// Crest scattering (0.19 water material): looking through a wave towards the sun, the thin
// crest lets light through and glows a pale green-turquoise - the one thing that makes a
// swell read as water rather than a moving relief. Strongest on the lifted part of the
// surface, against the light, in the detail band, and gone in a flat calm.
  float lift = clamp(v_wpos.y / max(0.35 * u_wave_scale * max(v_amp, 0.05), 1e-3), 0.0, 1.0);
  float against = pow(max(dot(vdir, -u_sun), 0.0), 2.5);
  float sss = lift * against * (0.35 + 0.65 * v_crest) * detail * clamp(v_amp, 0.0, 1.0) * (1.0 - fres);
  col3d += srgb_to_linear(vec3(0.18, 0.62, 0.52)) * u_suncol * sss * 0.45;
// --- foam (DESIGN_VISUAL.md step 6). Four sources, one white.
// Whitecaps: where the Gerstner displacement folds the surface - the horizontal Jacobian
// drops below 1 as the crest compresses - plus the steepest faces. Textured by the foam
// albedo so caps are patchy, not painted. Only the big waves count (v_jac/v_slope come from
// the wave sum, before the displacement map), and the amplitude gate keeps a calm sea bare.
  float sc = u_wave_scale;
  float sb = sc * u_shorescale;   // the shore bands' scale
  float ws = clamp(v_amp, 0.0, 1.4);
  float fold = smoothstep(0.93 - 0.05 * ws, 0.62 - 0.05 * ws, v_jac);
  float steep = smoothstep(0.04, 0.12, v_slope) * 0.7;
// 0.22.3: whitecaps only where a wave is a wave on screen. From the regional zoom the strategic
// swell's folds smeared into kilometre-long white diagonals across every bay; past ~30 units
// the caps fade and the sea keeps its glitter alone.
// 0.22.4: the caps are *masked by the foam texture*, not merely tinted by it. v_jac and v_slope
// are per-vertex on an 8 px tessellation cell and the threshold is sharp, so a cap drawn from
// them alone is a block the size of the cell and a line along every patch border (the staged
// battle's sea was tiled with them); through the texture's bright tenth it is a patch of foam
// at texel resolution, and the cells never show.
  float capmask = smoothstep(0.05, 0.40, lum2);
  float caps = max(fold, steep) * smoothstep(0.25, 0.8, ws) * capmask * detail * u_capzoom * (u_local_depth > 0.0 ? 0.6 : 1.0);
// Surf: in the shoaling band the steepened waves break into a foam sheet that runs up the
// beach. The shore direction is the depth gradient; the sheet's texture scrolls along it.
  vec2 hp = v_wpos.xz; float he = 0.9 * sc;
  vec2 g = vec2(depth_at(hp + vec2(he, 0.0)) - depth_at(hp - vec2(he, 0.0)), depth_at(hp + vec2(0.0, he)) - depth_at(hp - vec2(0.0, he)));
  vec2 shoredir = length(g) > 1e-4 ? -normalize(g) : vec2(0.0);
// 0.22.3: the breaker zone only. The old band ran out to 1.8 units of depth, which on the
// Adriatic shelf is the whole sea, so every crest line carried a sheet of surf and the bays
// were striped white from the regional view.
  float band = smoothstep(0.42 * sb, 0.12 * sb, depth) * smoothstep(0.0, 0.05 * sb, depth) * max(u_capzoom, 0.35);
  float surf_tex = smoothstep(0.22, 0.6, dot(texture(u_albedo, p * 0.7 + shoredir * (t * 0.22) + u_drift1 * 0.5).rgb, vec3(0.3, 0.5, 0.2)));
  float surf = band * (0.3 + 0.7 * max(v_crest, fold)) * (0.45 + 0.75 * surf_tex) * smoothstep(0.1, 0.5, ws + 0.3) * detail;
// Shoreline: the thin lapping line at the waterline, as before.
  float foam = (1.0 - smoothstep(0.0, 0.26 * sb, depth)) * (0.55 + 0.45*sin(depth*30.0/sb - t*2.0 + w1)) * detail;
// The coast the waves meet (0.22, the rings' shore mask): R cliff, G beach, B marsh, A exposure.
// Waves come in *sets* - a forty-second swell of bigger ones, then a lull - and every set's
// wave breaks differently on each coast: on a cliff it slams the face and bursts into a sheet
// of white at its foot (the terrain shader throws the spray up the rock); on a beach the surf
// sheet runs further up the sand with each set and drains back; a marsh takes no surf at all.
  vec4 sh = shore_at(v_wpos.xz);
  float sets = 0.55 + 0.45 * sin(t * 0.157);
  float wph = t * 0.8 + snoise(hp * 6.0 / sc) * 6.2832;
  float crash = pow(max(sin(wph), 0.0), 3.0) * sets;
  float cliffband = (1.0 - smoothstep(0.0, 0.9 * sb, depth)) * sh.r * sh.a;
  float crashf = cliffband * (0.35 + 0.65 * crash) * (0.5 + 0.8 * surf_tex) * detail;
  surf *= 1.0 + 0.6 * sh.g * sets;
  float marshk = 1.0 - 0.85 * sh.b;
// Ships: white only where the ship is going. The bow wave the vertex stage raised breaks
// at its crest (v_shipw), the stem throws spray ahead, and the forward half of the hull
// breaks the water along its shoulders. Nothing trails astern: the long V wake this used to
// draw read as a scar on the sea, and at map zooms every ship towed one across the ocean.
  float shipk = 1.0;
// 1 - union of the ships' foam: overlapping splashes never add up to a sheet
  for (int i = 0; i < u_nships; ++i) {
    vec2 dxz = v_wpos.xz - u_ships[i].xy; float L = max(u_shiplen[i], 0.2);
    if (dot(dxz, dxz) > 2.0 * L * L) continue;
    float ch = cos(u_ships[i].z), sh = -sin(u_ships[i].z);
// forward = (cos h, -sin h), as scene.c
    float along = dxz.x * ch + dxz.y * sh, across = abs(-dxz.x * sh + dxz.y * ch);
    float spd = clamp(u_ships[i].w * 10.0, 0.15, 1.0);
// the stem's own splash: a tight patch of white at the cutwater, broken up by the foam texture
    float bowx = (along - 0.46 * L) / (0.13 * L), bowz = across / (0.14 * L);
    float bow = exp(-(bowx * bowx + bowz * bowz)) * (0.5 + 0.5 * min(ws, 1.0)) * (0.5 + 0.9 * lum2) * spd;
// the waterline break along the forward half only, a thin band chopped up by the ripple
// detail so it reads as water slapping the planking; the quarter runs clean
    float hull = smoothstep(-0.10 * L, 0.05 * L, along) * (1.0 - smoothstep(0.36 * L, 0.46 * L, along))
               * smoothstep(0.155 * L, 0.185 * L, across) * (1.0 - smoothstep(0.21 * L, 0.26 * L, across))
               * smoothstep(0.30, 0.62, lum2) * 0.8 * spd;
// streaks running out ahead of the stem with the bow wave, in the texture's own pattern
    float fwd = along - 0.46 * L;
    float ahead = step(0.0, fwd) * exp(-fwd / (0.16 * L)) * (1.0 - smoothstep(0.04 * L, 0.26 * L, across))
                * smoothstep(0.28, 0.60, lum1) * 0.7 * spd;
    shipk *= 1.0 - clamp(bow + hull + ahead, 0.0, 0.85);
  }
// the bow wave breaking: only its crest, and only in the texture's bright streaks, so it is a
// broken white line riding the heap of water and not a sheet laid over it
  float shipcrest = smoothstep(0.009, 0.015, v_shipw / max(sc, 1e-3)) * smoothstep(0.30, 0.58, lum2) * 0.8;
  shipk *= 1.0 - clamp(shipcrest, 0.0, 0.8);
  float shipf = (1.0 - shipk) * detail;
  vec3 foamcol = srgb_to_linear(vec3(0.93, 0.96, 0.98)) * (u_ambsky * 1.0 + u_suncol * 0.35);
  float foamall = clamp((caps * 0.9 + (surf * 0.8 + foam * 0.7 + crashf) * marshk + shipf) * u_foam, 0.0, 1.0);
  col3d = mix(col3d, foamcol, foamall);
  col3d = mix(col3d, srgb_to_linear(vec3(0.90,0.93,0.95)) * (u_ambsky * 0.9 + u_suncol * 0.30), streak * 0.10 * u_foam);
  float caps_a = foamall;
  vec3 mapc = mix(vec3(0.62,0.72,0.80), vec3(0.50,0.62,0.74), 1.0 - exp(-depth*0.8));
  float gx = abs(fract(lon/10.0 + 0.5) - 0.5) * 10.0, gy = abs(fract(lat/10.0 + 0.5) - 0.5) * 10.0;
  float grid = 1.0 - smoothstep(0.0, 0.06, min(gx, gy));
  float gx5 = abs(fract(lon/5.0 + 0.5) - 0.5) * 5.0, gy5 = abs(fract(lat/5.0 + 0.5) - 0.5) * 5.0;
  float grid5 = (1.0 - smoothstep(0.0, 0.035, min(gx5, gy5))) * 0.5;
  mapc = mix(mapc, vec3(0.35,0.42,0.52), max(grid, grid5) * 0.8);
  mapc = mix(mapc, vec3(0.30,0.40,0.52), (1.0 - smoothstep(0.0, 0.6, depth))*0.55);
  vec3 col = fog_apply(mix(col3d, srgb_to_linear(mapc) * u_daylight, u_mapness), dist, u_mapness, srgb_to_linear(u_sky));
// transparency: the terrain below has already been drawn, so shallow water reveals the
// seabed and deep water goes opaque; the map view stays solid parchment
  if (u_sea_debug > 0.5) { fragColor = vec4(clamp(depth * 0.5, 0.0, 1.0), v_h > 0.35 ? 1.0 : 0.0, refr_on, 1.0); return; }
  float alpha = mix(0.26, 0.92, smoothstep(0.0, 1.6 * u_wave_scale, depth));
  if (u_local_depth > 0.0) alpha = 0.72;   // the battle and harbour views: the water is clearer, the hull shows through the surface (0.22.4)
// clearer shallows, seabed reads through a little further out
  alpha = max(alpha, caps_a * 0.9);
// foam stays visible over the seabed
  alpha = mix(alpha, 1.0, max(u_mapness, refr_on));
// the map modes (0.22): depth paints the water itself; the thematic modes dim it to a flat
// grey-blue so the land carries the colour
  if (u_mapmode > 0 && u_local_depth <= 0.0) {
// the depth read per fragment from the heightmap and the rings, not the coarse sea grid's
// interpolated vertex height - that showed every far-grid cell as a block of one colour
    vec3 mc = u_mapmode == 1 ? mm_depth_color(depth_at(v_wpos.xz)) : vec3(0.30, 0.36, 0.44);
// replaced outright, lit only by the swell's own shading: any share of the refracted frame
// would show the terrain chunks drawn under the shallows as 64-cell blocks
    col = srgb_to_linear(mc) * max(u_daylight, 0.6) * (0.80 + 0.20 * max(dot(wn, u_sun), 0.0));
    alpha = 1.0;
  }

// refraction composites the background itself: opaque
  fragColor = vec4(col, alpha);
}
