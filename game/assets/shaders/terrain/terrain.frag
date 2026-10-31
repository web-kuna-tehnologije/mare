in vec3 v_wpos; in vec2 v_uv; in float v_h;
uniform sampler2D u_hm; uniform sampler2D u_terr; uniform vec2 u_texel; uniform float u_texel_world;
uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_k; uniform vec4 u_geo;
uniform sampler2D u_ao; uniform float u_ao_on;
// baked horizon occlusion (terrain_init): 1 = open sky, lower in valleys
uniform vec3 u_nation_colors[11]; uniform float u_daylight; uniform float u_regions; uniform vec2 u_terr_texel;
uniform float u_flatten;
uniform sampler2D u_atlas; uniform float u_has_atlas;
// ground material atlas (assets/terrain/terrain_textures.png, 6x3 cells)
uniform sampler2D u_grain;
// the procedural surface-grain normal map (detail.c): micro relief on the close ground
uniform float u_time;
#include "terrain/detail.glsl"
#include "terrain/chunks.glsl"
out vec4 fragColor;
// One cell of the atlas, tiled: the uv wraps inside the cell by hand (an atlas cannot use
// the sampler's repeat) and the derivatives come from the *unwrapped* coordinate, so the
// mip level is continuous across the wrap and the seam does not show. Cells are inset a
// little to keep the neighbour's texels out of the bilinear footprint.
vec3 atlas_cell(int cell, vec2 p) {
  vec2 f = fract(p);
  vec2 cuv = (vec2(float(cell - (cell / 6) * 6), float(cell / 6)) + 0.012 + f * 0.976) / vec2(6.0, 3.0);
  vec2 ddx = dFdx(p) * (0.976 / 6.0), ddy = dFdy(p) * (0.976 / 6.0);
  return textureGrad(u_atlas, cuv, ddx, ddy).rgb;
}
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }
#include "common/color.glsl"
#include "common/pbr.glsl"
#include "common/light.glsl"
#include "common/fog.glsl"
#include "common/nmap.glsl"
#include "common/mapmode.glsl"
// The chunk data's class neighbourhood (chunks.glsl), set up once in main: the four classes
// around the fragment and their weights, and a ragged offset so two classes meet along a
// broken line rather than the 3 km sample grid.
vec4 g_cids = vec4(0.0), g_cwts = vec4(0.0); float g_rag = 0.0;
float wcls(float id) {
  float w = ch_wclass(g_cids, g_cwts, id);
  return w <= 0.0 ? 0.0 : w >= 1.0 ? 1.0 : smoothstep(0.25, 0.75, w + g_rag);
}
void main(){
  float hl = texture(u_hm, v_uv - vec2(u_texel.x,0)).r, hr = texture(u_hm, v_uv + vec2(u_texel.x,0)).r;
  float hu = texture(u_hm, v_uv - vec2(0,u_texel.y)).r, hd = texture(u_hm, v_uv + vec2(0,u_texel.y)).r;
  float gx = (hr-hl)/(2.0*u_texel_world), gz = (hd-hu)/(2.0*u_texel_world);
// The streamed detail moves the surface, so it has to light it too: without this the relief
// the rings add is shaded by the base map's normals and reads as flat painted ground.
  float dk = 1.0 - u_flatten;
  float estep = det_step(v_wpos.xz);
  if (dk > 0.01) {
    float e = estep;
    gx += dk * (det_at(v_wpos.xz + vec2(e,0.0)) - det_at(v_wpos.xz - vec2(e,0.0))) / (2.0*e);
    gz += dk * (det_at(v_wpos.xz + vec2(0.0,e)) - det_at(v_wpos.xz - vec2(0.0,e))) / (2.0*e);
  }
  vec3 n = normalize(vec3(-gx, 1.0, -gz));
  float h = v_h; float lat = -v_wpos.z / u_k; float lon = v_wpos.x / u_k;
// --- the chunk data (0.22): the export's land-cover classes, material weights and normal
// (gfx/chunkatlas.h). Everything below keeps its procedural rule as the fallback and takes the
// data's word where a chunk is resident and the sample is covered.
  vec2 cuv, clocal, ccell; bool cdata = ch_uv(lon, lat, cuv, clocal, ccell);
  float ccover = 0.0; vec4 csplat = vec4(0.0);
  if (cdata) {
    ch_classes(clocal, ccell, g_cids, g_cwts, ccover);
    csplat = ch_splat(cuv) * ccover;
    g_rag = (noise(v_wpos.xz * 2.3 + 5.0) - 0.5) * 0.6 + (noise(v_wpos.xz * 7.1 + 9.0) - 0.5) * 0.25;
// the export's normal lights the ground wherever the rings are coarser than its samples -
// outside the near ring the far ring's texel is 4.3 km and the base map's 7 km against the
// data's 3 km; inside it the finite difference of the composed ground is already finer
    float kn = smoothstep(0.2, 0.5, estep) * ccover * dk;
    if (kn > 0.001) n = normalize(mix(n, ch_normal(cuv), kn));
  }
  float wtrees = wcls(10.0), wshrub = wcls(20.0), wcrops = wcls(40.0), wbuilt = wcls(50.0), wbare = wcls(60.0);
  float wsnowc = wcls(70.0), wlake = wcls(80.0), wmarsh = wcls(90.0), wmangrove = wcls(95.0), wmoss = wcls(100.0);
// the coast this fragment stands on (streamview.cc's shore mask): cliff, beach, marsh, exposure
  vec4 sh = shore_at(v_wpos.xz) * dk;
  wmarsh = max(wmarsh, sh.b);
  float slope = 1.0 - n.y;
  float nz = noise(v_wpos.xz*0.9) * 0.6 + noise(v_wpos.xz*3.7)*0.4;
// --- the palette (sRGB), from the aerial references in assets/example (0.22): pale warm
// beaches, olive scrub on the sunny slopes, dark woods in the gullies and on the shaded
// sides, pale karst limestone in the Mediterranean belt and dark basalt-like rock elsewhere,
// warm bare crests, scree at the foot of every cliff
  vec3 sand      = mix(vec3(0.87,0.81,0.65), vec3(0.80,0.74,0.58), nz);
  vec3 wetsand   = vec3(0.55,0.50,0.40);
  vec3 grass     = mix(vec3(0.33,0.50,0.22), vec3(0.22,0.38,0.16), nz);
  vec3 scrub     = mix(vec3(0.47,0.51,0.29), vec3(0.61,0.58,0.36), nz);
  vec3 forest    = mix(vec3(0.13,0.26,0.12), vec3(0.19,0.33,0.15), nz);
  vec3 dry       = mix(vec3(0.58,0.53,0.36), vec3(0.48,0.45,0.31), nz);
  vec3 limestone = mix(vec3(0.77,0.73,0.65), vec3(0.68,0.64,0.57), nz);
  vec3 basalt    = mix(vec3(0.41,0.36,0.31), vec3(0.33,0.29,0.26), nz);
  vec3 crest     = vec3(0.53,0.43,0.32);
  vec3 scree     = mix(vec3(0.61,0.57,0.50), vec3(0.52,0.49,0.43), nz);
  vec3 snow      = vec3(0.93,0.94,0.96);
  vec3 desert    = mix(vec3(0.86,0.74,0.48), vec3(0.80,0.66,0.42), nz);
  vec3 tundra    = mix(vec3(0.55,0.58,0.48), vec3(0.70,0.72,0.68), nz);
  vec3 marshc    = mix(vec3(0.36,0.44,0.25), vec3(0.27,0.32,0.22), nz);
  vec3 mangrove  = mix(vec3(0.12,0.28,0.16), vec3(0.17,0.33,0.18), nz);
  vec3 lake      = vec3(0.12,0.24,0.31);
  float med = smoothstep(32.0, 35.5, abs(lat)) * (1.0 - smoothstep(44.5, 47.5, abs(lat))) * smoothstep(-12.0, -6.0, lon) * (1.0 - smoothstep(40.0, 44.0, lon));
// --- the weights
// cliffs: rock wherever the ground is steep; limestone in the Mediterranean, basalt elsewhere,
// warming to a bare brown crest high up; horizontal beds broken by noise on the steep faces.
// The export's rock weight adds bare rock where the ground is flat too (karst, lava fields),
// and the shore mask's cliff coasts are rock down to the water.
  float wrock = smoothstep(0.16, 0.40, slope);
  wrock = max(wrock, csplat.g * (0.35 + 0.65 * smoothstep(0.03, 0.15, slope)));
  wrock = max(wrock, sh.r * smoothstep(0.0, 0.6, h) * 0.9);
  vec3 rock = mix(basalt, limestone, med);
  rock = mix(rock, crest, smoothstep(2.5, 6.0, h) * 0.6);
// bedding: a few beds per unit of height, their phase warped by two scales of noise so no
// two faces band alike, a tonal swing of a few percent, only on steep rock, and only up
// close - from the regional view a regular band reads as a contour line, not a cliff
  float dist_s = distance(v_wpos, u_campos);
  float strat = 0.5 + 0.5 * sin(v_wpos.y * 9.0 + noise(v_wpos.xz * 1.3) * 6.0 + noise(v_wpos.xz * 5.1) * 2.0);
  float stratk = mix(1.0, 0.95 + 0.09 * strat, wrock * smoothstep(0.25, 0.45, slope) * (1.0 - smoothstep(18.0, 45.0, dist_s)));
// scree: the talus at a cliff's foot - moderate slope below steep ground, low on the hill
  float wscree = smoothstep(0.07, 0.17, slope) * (1.0 - smoothstep(0.22, 0.40, slope)) * (1.0 - smoothstep(0.4, 1.4, h)) * 0.8;
// the beach: low, gentle ground. Its width is the slope's - a flat coast has a wide strand,
// a steep one none - and the shore mask's beach coasts are sand whatever the slope says.
// Sea water runs up the strand and back (the swash): the wet band breathes with the sets.
  float sets  = 0.55 + 0.45 * sin(u_time * 0.157);
  float swash = 0.5 + 0.5 * sin(u_time * 0.8 + noise(v_wpos.xz * 3.0) * 6.2832);
  float wbeach = (1.0 - smoothstep(0.04, 0.30, h)) * (1.0 - smoothstep(0.06, 0.20, slope));
  wbeach = max(wbeach, sh.g * (1.0 - smoothstep(0.10, 0.36, h)));
  wbeach = max(wbeach, csplat.b * (1.0 - smoothstep(0.25, 0.9, h)) * (1.0 - wrock));
  float runup = 0.07 + (0.035 + 0.045 * sets) * swash * sh.g;
  float wwet  = 1.0 - smoothstep(0.0, runup, h);
  float foamline = sh.g * sh.a * (1.0 - smoothstep(0.0, 0.012, abs(h - runup))) * (0.4 + 0.6 * noise(v_wpos.xz * 9.0 + u_time * 0.2));
// vegetation: grass on the lowlands, scrub where the belt is Mediterranean, woods in the
// gullies and on the shaded sides. Aspect comes from the normal: +z is south, so a face with
// n.z < 0 looks north and keeps its shade. The export's tree cover puts the woods where they
// are - the gullies and shaded sides only shape the edges.
  float wgrass = smoothstep(0.12, 0.5, h);
  float gully  = smoothstep(0.42, 0.78, noise(v_wpos.xz * 0.5 + 7.0) * 0.6 + noise(v_wpos.xz * 1.9 + 3.0) * 0.4);
  float shade  = clamp(0.5 - 0.5 * n.z / max(length(n.xz), 1e-3), 0.0, 1.0) * smoothstep(0.04, 0.18, slope);
  float wforest = clamp(0.55 * gully + 0.45 * shade, 0.0, 1.0) * (1.0 - smoothstep(3.0, 5.0, h)) * (0.55 + 0.45 * med) * wgrass;
  wforest = max(wforest * (1.0 - 0.6 * ccover), wtrees * (0.7 + 0.3 * gully) * (1.0 - smoothstep(0.0, 0.08, max(0.0, -h))));
  float wdry  = smoothstep(2.5, 5.0, h);
  wdry = max(wdry, wbare * 0.85);
  float wsnow = smoothstep(6.5, 8.5, h) * (1.0 - smoothstep(0.55, 0.75, slope));
  wsnow = max(wsnow, max(wsnowc, csplat.a) * (1.0 - smoothstep(0.55, 0.8, slope)));
  float under = 1.0 - smoothstep(-0.02, 0.02, h);
// the lowland patchwork (0.22): fields, hay meadows and rough grazing at the scale of the
// Designer sheet's countryside - a low-frequency noise picks between green turf, a drier
// straw-coloured grass and a darker meadow, so the plain is a quilt rather than one carpet.
// Where the export says cropland the quilt is the whole ground: stubble, young green corn and
// ploughed earth in strips, hedged by the ragged class edge.
  float field  = noise(v_wpos.xz * 4.0 + 13.0) * 0.65 + noise(v_wpos.xz * 0.34 + 13.0) * 0.35;   // a field is a kilometre or two; the regional term keeps whole districts drier or greener
  float field2 = noise(v_wpos.xz * 9.0 + 41.0);
  vec3 hay    = mix(vec3(0.66,0.58,0.32), vec3(0.74,0.66,0.40), nz);
  vec3 meadow = mix(vec3(0.27,0.43,0.18), vec3(0.31,0.47,0.20), nz);
  vec3 plough = mix(vec3(0.42,0.33,0.24), vec3(0.50,0.40,0.29), nz);
  vec3 lowland = mix(grass, hay, smoothstep(0.52, 0.70, field) * 0.85);
  lowland = mix(lowland, meadow, smoothstep(0.30, 0.46, field) * (1.0 - smoothstep(0.52, 0.60, field)) * 0.7);
  lowland = mix(lowland, grass * 0.92, smoothstep(0.62, 0.80, field2) * 0.4);
  float strips = noise(vec2(v_wpos.x * 11.0 + v_wpos.z * 3.0, v_wpos.z * 1.7) + 23.0);
  vec3 crops = mix(lowland, plough, smoothstep(0.55, 0.72, strips) * 0.7);
  crops = mix(crops, hay * 1.05, smoothstep(0.30, 0.42, strips) * (1.0 - smoothstep(0.48, 0.55, strips)) * 0.6);
  float wpatch = (1.0 - smoothstep(0.14, 0.32, slope)) * (1.0 - smoothstep(2.0, 3.6, h));
  grass = mix(grass, lowland, wpatch);
  grass = mix(grass, crops, wcrops * 0.9);
// macchia: the olive scrub belongs to the dry southern belt (Sicily, the Aegean, Andalusia,
// the Maghreb), not to the Po plain or the Venetian lagoon, which are farmland - and
// wherever the export's class is shrubland
  float scrubk = smoothstep(32.0, 36.0, abs(lat)) * (1.0 - smoothstep(41.5, 44.5, abs(lat))) * smoothstep(-12.0, -6.0, lon) * (1.0 - smoothstep(40.0, 44.0, lon));
  scrubk = max(scrubk * (1.0 - 0.5 * ccover), wshrub);
// built-up ground: warm roof tile and pale stone, mottled at the scale of a town
  vec3 town = mix(vec3(0.56,0.48,0.40), vec3(0.60,0.40,0.32), smoothstep(0.35, 0.65, field2));
  town = mix(town, vec3(0.70,0.66,0.58), smoothstep(0.55, 0.75, noise(v_wpos.xz * 16.0)) * 0.5);
// --- composition, low to high
  vec3 c = mix(grass, scrub, scrubk * 0.75);
  c = mix(c, forest, wforest);
  c = mix(c, dry, wdry * (1.0 - 0.5 * wforest));
  c = mix(c, sand, wbeach);
  c = mix(c, wetsand, wbeach * wwet);
  c = mix(c, scree, wscree);
  c = mix(c, rock, wrock);
  c = mix(c, rock * 0.62, (1.0 - smoothstep(0.0, 0.35, h)) * smoothstep(0.12, 0.3, slope) * 0.85);
// wet, dark rock in the splash zone
  c = mix(c, marshc, wmarsh * 0.9 * (1.0 - smoothstep(0.3, 0.8, h)));
  c = mix(c, mangrove, wmangrove * 0.9);
  c = mix(c, town, wbuilt * 0.8);
  c = mix(c, snow, wsnow);
// the sea floor: the beach sand runs on under the water and shades into silt and weed where
// the shelf drops - it is this pale bottom that the turquoise shallows are seen through
  vec3 seabed = mix(sand * 0.92, vec3(0.30, 0.40, 0.36), smoothstep(-0.12, -1.0, h));
  seabed = mix(seabed, vec3(0.28, 0.26, 0.20), wmarsh * 0.8);   // mud flats
  c = mix(c, seabed, under);
// deserts: sahara, arabia, northern mexico, atacama - and the export's sand where it is high
// and dry (an erg, not a beach)
  float des = 0.0;
  des = max(des, (1.0-smoothstep(30.0,33.0,lat)) * smoothstep(13.0,16.0,lat) * smoothstep(-18.0,-14.0,lon) * (1.0-smoothstep(36.0,40.0,lon)));
  des = max(des, smoothstep(14.0,18.0,lat)*(1.0-smoothstep(30.0,34.0,lat))*smoothstep(35.0,38.0,lon));
  des = max(des, smoothstep(22.0,25.0,lat)*(1.0-smoothstep(32.0,35.0,lat))*smoothstep(-116.0,-112.0,lon)*(1.0-smoothstep(-103.0,-100.0,lon)));
  des = max(des, smoothstep(-30.0,-26.0,lat)*(1.0-smoothstep(-16.0,-14.0,lat))*smoothstep(-72.0,-70.0,lon)*(1.0-smoothstep(-66.0,-64.0,lon)));
  float wdes = des * smoothstep(0.12, 0.5, h) * (1.0 - smoothstep(5.0, 7.0, h));
  wdes = max(wdes, csplat.b * smoothstep(0.3, 0.9, h) * (1.0 - wrock) * (1.0 - wtrees) * 0.9);
  c = mix(c, desert, wdes);
  float cold = smoothstep(52.0, 64.0, abs(lat));
  float wtundra = max(cold * smoothstep(0.3,1.0,h) * (1.0 - 0.5 * ccover), wmoss);
  c = mix(c, tundra, wtundra);
  c = mix(c, snow, smoothstep(64.0, 70.0, abs(lat)) * 0.8 * (1.0 - ccover * (1.0 - wsnow)));
// inland water (the export's permanent-water class, lakes and the broad rivers): a flat dark
// surface in the hollow; rivers as carved channels with flowing water are the next step
  float wwater = wlake * (1.0 - smoothstep(0.0, 0.12, slope));
  c = mix(c, lake, wwater);
// Textured ground (0.17, re-cast 0.22): the atlas materials follow the same weights as the
// flat colours, at two scales (fine repeats for the city and battle tiers, a coarse repeat
// that survives the regional view), triplanar for the rock so cliff faces are not smeared,
// faded out with distance so the strategic map keeps its clean biome colours.
  float dist0 = distance(v_wpos, u_campos);
  float tdet = (1.0 - smoothstep(90.0, 420.0, dist0)) * u_has_atlas * (1.0 - u_mapness);
  if (tdet > 0.01) {
    vec3 tw = pow(abs(n), vec3(6.0)); tw /= (tw.x + tw.y + tw.z);
    vec2 pxz = v_wpos.xz * 1.6, mxz = v_wpos.xz * 0.21;
    float far = smoothstep(8.0, 45.0, dist0);
    vec3 grassT  = mix(mix(atlas_cell(1, pxz), atlas_cell(13, pxz), 0.35), atlas_cell(1, mxz), far);
    vec3 hayT    = mix(mix(atlas_cell(2, pxz), atlas_cell(13, pxz), 0.55), atlas_cell(9, mxz), far);   // soil through thin dry grass: stubble
    vec3 meadowT = mix(atlas_cell(7, pxz), atlas_cell(7, mxz), far);
    vec3 soilT   = mix(atlas_cell(2, pxz), atlas_cell(12, mxz), 0.5 + 0.5 * far);
    float wp = max(wpatch, wcrops);
    grassT = mix(grassT, hayT,    smoothstep(0.52, 0.70, field) * 0.85 * wp);
    grassT = mix(grassT, meadowT, smoothstep(0.30, 0.46, field) * (1.0 - smoothstep(0.52, 0.60, field)) * 0.7 * wp);
    grassT = mix(grassT, soilT,   smoothstep(0.55, 0.72, strips) * 0.7 * wcrops);
    vec3 scrubT  = mix(mix(atlas_cell(13, pxz), atlas_cell(2, pxz), 0.35), atlas_cell(9, mxz), far);
    vec3 forestT = mix(mix(atlas_cell(14, pxz), atlas_cell(11, pxz), 0.45), atlas_cell(14, mxz), far);
    vec3 sandT   = mix(atlas_cell(3, pxz), atlas_cell(10, mxz), far);
    vec3 dryT    = mix(mix(atlas_cell(9, pxz), atlas_cell(2, pxz), 0.4), atlas_cell(9, mxz), far);
    vec3 screeT  = mix(mix(atlas_cell(9, pxz), atlas_cell(4, pxz), 0.5), atlas_cell(4, mxz), far);
    vec3 rockT   = atlas_cell(0, mix(pxz, mxz, far)) * tw.y + atlas_cell(0, mix(v_wpos.xy * 1.6, v_wpos.xy * 0.21, far)) * tw.z + atlas_cell(0, mix(v_wpos.zy * 1.6, v_wpos.zy * 0.21, far)) * tw.x;
    vec3 desertT = mix(atlas_cell(6, pxz), atlas_cell(5, mxz), 0.5 + 0.5 * far);
    vec3 tundraT = mix(atlas_cell(14, pxz), atlas_cell(16, mxz), 0.4 + 0.5 * far);
    vec3 marshT  = mix(mix(atlas_cell(11, pxz), atlas_cell(7, pxz), 0.5), atlas_cell(11, mxz), far);
    vec3 townT   = mix(atlas_cell(15, pxz), atlas_cell(15, mxz), far);
    vec3 t = mix(grassT, scrubT, scrubk * 0.75);
    t = mix(t, forestT, wforest);
    t = mix(t, dryT, wdry * (1.0 - 0.5 * wforest));
    t = mix(t, sandT, wbeach);
    t = mix(t, sandT * 0.7, wbeach * wwet);
    t = mix(t, screeT, wscree);
    t = mix(t, rockT, wrock);
    t = mix(t, rockT * 0.6, (1.0 - smoothstep(0.0, 0.35, h)) * smoothstep(0.12, 0.3, slope) * 0.85);
    t = mix(t, marshT, wmarsh * 0.9 * (1.0 - smoothstep(0.3, 0.8, h)));
    t = mix(t, forestT * 0.8, wmangrove * 0.9);
    t = mix(t, townT, wbuilt * 0.8);
    t = mix(t, mix(rockT, vec3(0.9, 0.92, 0.95), 0.8), wsnow);
    t = mix(t, sandT * 0.9, under * (1.0 - smoothstep(-0.12, -1.0, h)));
    t = mix(t, desertT, wdes);
    t = mix(t, tundraT, wtundra);
    vec3 ts = linear_to_srgb(t);
// the atlas is sRGB-sampled (linear); the colour mix below is in sRGB
    c = mix(c, ts * (0.55 + 0.45 * (c / max(ts, vec3(0.02)))), 0.78 * tdet * (1.0 - wwater));
// keep the biome's hue, take the material's grain
  }
  c *= stratk;
// The cliff's foot (0.22): each set's wave bursts on the rock and throws spray up the face -
// a sheet of white that climbs, hangs and drains in streaks - above a band of wet dark rock
// the sea keeps slick. Only on exposed cliff coasts, only near the camera, never on the map.
  float nearc = (1.0 - smoothstep(60.0, 160.0, dist0)) * (1.0 - u_mapness);
  float wph = u_time * 0.8 + noise(v_wpos.xz * 6.0) * 6.2832;
  float crash = pow(max(sin(wph), 0.0), 3.0) * sets;
  float streaks = smoothstep(0.30, 0.70, noise(vec2(v_wpos.x * 18.0 + v_wpos.z * 7.0, v_wpos.y * 9.0 - u_time * 0.5)));
  float spray = sh.r * sh.a * nearc * (0.3 + 0.7 * crash) * (1.0 - smoothstep(0.05, 0.22 + 0.30 * sets * crash, h)) * (0.35 + 0.65 * streaks);
  c = mix(c, vec3(0.92, 0.95, 0.97), clamp(spray, 0.0, 1.0) * 0.85);
  c = mix(c, vec3(0.95, 0.97, 0.98), clamp(foamline * nearc, 0.0, 1.0) * 0.7);
// Ground materials (0.19): each surface has its own roughness - grass and scrub matte, dry
// soil a little less, bare rock and shingle tighter, snow a soft gloss, and the rock in the
// splash zone wet and dark-glossy - and, close in, a micro-relief normal from the grain map
// so the beach and the rock faces stop reading as painted planes under a low sun.
  float wet = (1.0 - smoothstep(0.0, 0.35, h)) * smoothstep(0.12, 0.3, slope);
  wet = max(wet, sh.r * (1.0 - smoothstep(0.0, 0.45 + 0.2 * sets, h)));
  float rough = 0.95;
  rough = mix(rough, 0.84, wdry);
  rough = mix(rough, 0.72, wrock);
  rough = mix(rough, 0.80, wbeach);                                   // beach sand
  rough = mix(rough, 0.45, wbeach * wwet);                            // the wet strand
  rough = mix(rough, 0.90, wforest);
  rough = mix(rough, 0.55, wsnow);
  rough = mix(rough, 0.40, wet * 0.8);
  rough = mix(rough, 0.18, wwater);                                   // still water
  float micro = (1.0 - smoothstep(12.0, 60.0, dist0)) * (1.0 - u_mapness) * (1.0 - wwater);
  if (micro > 0.01) {
    float mstr = mix(0.25, 0.55, wrock) * micro;
    n = nmap_triplanar(u_grain, v_wpos * 1.3, n, 1.0, mstr);
  }
  n = normalize(mix(n, vec3(0.0, 1.0, 0.0), wwater));
// Rivers (0.22): where the ring's river texture says water, the carved bed is painted as the
// river itself - a dark green-blue surface reflecting the sky, ripples drawn along the flow
// and carried downstream by the clock, a glossy roughness - and the bank just above it stays
// wet and dark. Faded with distance like the rest of the detail; a line of blue in the map modes.
  vec4 rv = river_at(v_wpos.xz);
  float rk = rv.r * (1.0 - u_mapness) * (1.0 - under);
  if (rk > 0.01) {
    vec2 fdir = normalize(rv.gb * 2.0 - 1.0 + vec2(1e-4));
    vec2 fperp = vec2(-fdir.y, fdir.x);
    float along = dot(v_wpos.xz, fdir) - u_time * 0.35, across = dot(v_wpos.xz, fperp);
    float rip = noise(vec2(along * 28.0, across * 90.0)) * 0.6 + noise(vec2(along * 70.0 + 3.0, across * 160.0)) * 0.4;
    float rfade = 1.0 - smoothstep(40.0, 160.0, dist0);
    vec3 river = mix(vec3(0.13, 0.25, 0.27), vec3(0.30, 0.42, 0.42), rip * 0.5);
    river = mix(river, u_sky * 0.55, 0.25);
    c = mix(c, river, rk);
    rough = mix(rough, 0.12 + 0.1 * rip, rk);
    vec3 rn = normalize(vec3((rip - 0.5) * 0.25 * rfade * fperp.x, 1.0, (rip - 0.5) * 0.25 * rfade * fperp.y));
    n = normalize(mix(n, rn, rk));
  }
  float diff = max(dot(n, u_sun), 0.0);
  vec3 vdir = normalize(u_campos - v_wpos);
  float viewdist = distance(v_wpos, u_campos);
  float ao = u_ao_on > 0.5 ? mix(1.0, texture(u_ao, v_uv).r, 0.85) : 1.0;
// the ground's own horizon: a valley in the lee of a range is out of the sun (det_sun_occ);
// faded with distance where the shadow would be a texel wide anyway
  { float ok = 1.0 - smoothstep(500.0, 900.0, viewdist);
    if (ok > 0.01) g_sun_occ = mix(1.0, det_sun_occ(v_wpos + n * 0.08, u_sun, u_hm, u_geo, u_k, u_mapness), ok); }
  vec3 lit = sun_shade_ao(srgb_to_linear(c), n, vdir, v_wpos, rough, 0.0, viewdist, ao);
// map style
  vec4 tt = texture(u_terr, v_uv);
  int nid = int(tt.r * 255.0 + 0.5);
  float dev = tt.g * 255.0 / 80.0;
  float rid = tt.b, border = 0.0;
  if (u_regions > 0.5) { border += float(abs(texture(u_terr, v_uv + vec2(u_terr_texel.x,0)).b - rid) > 0.001); border += float(abs(texture(u_terr, v_uv + vec2(0,u_terr_texel.y)).b - rid) > 0.001); }
  vec3 parch = vec3(0.87,0.80,0.64) * (0.85 + 0.15*nz);
  vec3 owner = nid > 0 ? u_nation_colors[min(nid,10)] : parch;
  vec3 mapc = mix(parch, owner, nid > 0 ? 0.55 : 0.0);
  mapc *= 0.75 + 0.25 * diff;
  mapc = mix(mapc, mapc*0.45, smoothstep(6.0, 9.0, h)*0.6);
  float coastline = 1.0 - smoothstep(0.05, 0.28, h);
  mapc = mix(mapc, vec3(0.35,0.28,0.18), coastline*0.8);
  if (u_regions > 0.5) { vec3 devc = mix(vec3(0.55,0.50,0.40), vec3(0.95,0.85,0.45), clamp(dev/3.0,0.0,1.0)); mapc = mix(mapc, devc, 0.55); mapc = mix(mapc, vec3(0.15,0.10,0.05), min(border,1.0)*0.8); }
  vec3 col = mix(lit, srgb_to_linear(mapc) * u_daylight, u_mapness);
// the map modes (0.22): a thematic colour laid over the ground, shaded by the sun so relief
// still reads, with the nation borders drawn in the political and influence modes
  if (u_mapmode > 0) {
// the territory texel is half a degree: read it through a noise jitter of up to half a texel
// so the borders wander like a drawn frontier instead of stepping along the grid
    vec2 jit = (vec2(noise(v_wpos.xz * 0.9 + 31.0), noise(v_wpos.xz * 0.9 + 77.0)) - 0.5) * 0.95 * u_terr_texel;
    vec2 juv = v_uv + jit;
    vec4 tj = texture(u_terr, juv); int njd = int(tj.r * 255.0 + 0.5);
    vec3 mc = mm_land_color(u_mapmode, tj, njd > 0 ? u_nation_colors[min(njd,10)] : parch, h, lon, lat, parch);
    float bord = 0.0;
    if (u_mapmode == 3 || u_mapmode == 4) {
      bord += float(abs(texture(u_terr, juv + vec2(u_terr_texel.x,0)).r - tj.r) > 0.001);
      bord += float(abs(texture(u_terr, juv + vec2(0,u_terr_texel.y)).r - tj.r) > 0.001); }
    mc = mix(mc, vec3(0.08, 0.06, 0.05), min(bord, 1.0) * 0.85);
    mc = mix(mc, vec3(0.25, 0.45, 0.80), rv.r * 0.9);
    mc *= 0.55 + 0.45 * diff;
    if (u_mapmode == 1) mc = mix(mc, mc * 0.6, 1.0 - smoothstep(0.0, 0.02, abs(fract(h / 0.5 + 0.5) - 0.5) * 0.5));   // contours every half unit
    col = mix(col, srgb_to_linear(mc) * max(u_daylight, 0.6), under > 0.5 ? 0.0 : 0.82);
  }
  col = fog_apply(col, viewdist, u_mapness, srgb_to_linear(u_sky));
  fragColor = vec4(col, 1.0);
}
