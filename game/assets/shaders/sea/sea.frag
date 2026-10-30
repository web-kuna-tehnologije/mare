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
// MARE_SEA_DEBUG=1: red = depth/2, green = land reading, blue = refraction on
out vec4 fragColor;
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/nmap.glsl"
#include "common/fog.glsl"
#include "terrain/detail.glsl"
#include "terrain/atlas.glsl"
float depth_at(vec2 pp) {
// water depth from the heightmap plus the streamed detail rings (staged scenes: constant)
  if (u_local_depth > 0.0) return u_local_depth;
  float lon = pp.x / u_k, lat = -pp.y / u_k;
  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));
  float h = texture(u_hm, uv).r + det_at(pp) * (1.0 - u_mapness);
  return max(-ha_shape(h, pp, 1.0 - u_mapness), 0.0);
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
  float detail = (1.0 - smoothstep(60.0 * u_wave_scale, 400.0 * u_wave_scale, dist)) * v_edge;
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
// North Atlantic, not a lagoon: a blue-grey body colour. The old pair ran to cyan because
// the shallow end was a tropical green-blue and the absorption killed red hardest.
  vec3 deep = vec3(0.035,0.070,0.115), shallow = vec3(0.21,0.285,0.325);
// Warm seas (0.22, from the aerial references in assets/example): the colour is a *ramp of
// stops* with depth, not one blend - pale aqua where sand shows through the first few metres,
// turquoise over the shelf, a saturated mid blue where the shelf drops, cobalt past it. The
// shelf edge is where the second and third stops meet, which is what draws the reef line the
// photos all have. North of ~45 degrees the sea keeps its grey-blue body.
  float warm = 1.0 - smoothstep(43.0, 53.0, abs(lat));   // the whole Mediterranean warm, the Channel and the North Sea grey
  vec3 st0 = vec3(0.40, 0.75, 0.73), st1 = vec3(0.10, 0.58, 0.66), st2 = vec3(0.03, 0.26, 0.52), st3 = vec3(0.02, 0.10, 0.30);
  vec3 warmc = depth < 0.22 ? mix(st0, st1, depth / 0.22)
             : depth < 0.90 ? mix(st1, st2, (depth - 0.22) / 0.68)
             :                mix(st2, st3, 1.0 - exp(-(depth - 0.90) * 0.9));
  vec3 coldc = mix(shallow, deep, 1.0 - exp(-depth*0.9));
  vec3 bodyc = mix(coldc, warmc, warm);
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
    vec3 absorb = vec3(5.5, 3.6, 2.2) / u_wave_scale;
    vec3 tr = exp(-absorb * depth * 1.05);
// Mix towards the *lit* water column, not the raw body colour: the sunlit seabed is twenty
// times brighter than an unlit blue, and won every shallow pixel however small `tr` was.
    refr = mix(body0, bg, tr); refr_on = 1.0;
  }
// authored surface: the foam albedo rides the swell layer (and a finer copy the chop), its
// bright streaks become whitecaps over the depth colour and its tonal variation breaks up
// the flat gradient; everything fades out with `detail` like the normals do
  vec3 alb1 = texture(u_albedo, p * 0.33 + u_drift1).rgb;
  vec3 alb2 = texture(u_albedo, p * 1.15 + u_drift2 + vec2(0.37, 0.11)).rgb;
  float lum1 = dot(alb1, vec3(0.3, 0.5, 0.2)), lum2 = dot(alb2, vec3(0.3, 0.5, 0.2));
  float streak = max(smoothstep(0.22, 0.58, lum1), 0.5 * smoothstep(0.24, 0.62, lum2)) * u_has_albedo * detail;
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
  fres = min(fres, 0.58 + 0.2 * u_calm);
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
  glint += u_suncol * sp * 0.22 * detail * (1.0 - 0.6 * u_calm);
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
  float ws = clamp(v_amp, 0.0, 1.4);
  float fold = smoothstep(0.93 - 0.05 * ws, 0.74 - 0.05 * ws, v_jac);
  float steep = smoothstep(0.04, 0.10, v_slope) * 0.7;
  float caps = max(fold, steep) * smoothstep(0.25, 0.8, ws) * (0.5 + 1.0 * lum2) * detail;
// Surf: in the shoaling band the steepened waves break into a foam sheet that runs up the
// beach. The shore direction is the depth gradient; the sheet's texture scrolls along it.
  vec2 hp = v_wpos.xz; float he = 0.9 * sc;
  vec2 g = vec2(depth_at(hp + vec2(he, 0.0)) - depth_at(hp - vec2(he, 0.0)), depth_at(hp + vec2(0.0, he)) - depth_at(hp - vec2(0.0, he)));
  vec2 shoredir = length(g) > 1e-4 ? -normalize(g) : vec2(0.0);
  float band = smoothstep(1.8 * sc, 0.5 * sc, depth) * smoothstep(0.0, 0.16 * sc, depth);
  float surf_tex = smoothstep(0.22, 0.6, dot(texture(u_albedo, p * 0.7 + shoredir * (t * 0.22) + u_drift1 * 0.5).rgb, vec3(0.3, 0.5, 0.2)));
  float surf = band * (0.3 + 0.7 * max(v_crest, fold)) * (0.45 + 0.75 * surf_tex) * smoothstep(0.1, 0.5, ws + 0.3) * detail;
// Shoreline: the thin lapping line at the waterline, as before.
  float foam = (1.0 - smoothstep(0.0, 0.26 * sc, depth)) * (0.55 + 0.45*sin(depth*30.0/sc - t*2.0 + w1)) * detail;
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
  float foamall = clamp((caps * 0.9 + surf * 0.8 + shipf + foam * 0.7) * u_foam, 0.0, 1.0);
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
  float alpha = mix(0.38, 0.92, smoothstep(0.0, 1.6 * u_wave_scale, depth));
// clearer shallows, seabed reads through a little further out
  alpha = max(alpha, caps_a * 0.9);
// foam stays visible over the seabed
  alpha = mix(alpha, 1.0, max(u_mapness, refr_on));
// refraction composites the background itself: opaque
  fragColor = vec4(col, alpha);
}
