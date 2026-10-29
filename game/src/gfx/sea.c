// Sea: a camera-centred detail grid with Gerstner waves over a world-wide flat plane, shaded
// from the heightmap (depth colour, foam, authored normal/foam textures); parchment blue with a
// graticule in map view.
//
// Geometry. The world grid (coarse, static, covering the whole map) is flat: at map and
// continental zoom that is all the sea needs to be. Inside a square of SEA_DETAIL_R units
// around the camera's look-at point the *detail grid* takes over - a grid whose cells grow
// exponentially with distance from the centre (SEA_WARP), so cells are ~0.25 units under the
// camera and ~13 units at the edge, which is what puts enough vertices under a ship to displace.
// The world pass discards inside that square, so the two never blend over each other.
//
// Waves. Four Gerstner waves (SEA_WAVES) displace the detail grid: crests sharpen, troughs
// flatten and the surface moves horizontally as well as vertically, which is what makes water
// read as water rather than as a wobbling sheet. The analytic surface derivatives give the
// normal, so no extra geometry is needed for shading; the authored normal map (game/assets/sea,
// cut seamless by tools/sea_textures.py) adds the ripple detail below the wave scale. Amplitude
// fades to zero towards the edge of the detail square, in shallow water (so waves never break
// through a beach) and in map view, so the two grids always meet on a flat surface.
//
// Scale. `u_wave_scale` stretches the whole wave field: the world uses 1 (ships are ~2 units
// long, waves ~2-6 units), the detail view passes DV_SHIP_SCALE so the same sea reads correctly
// at 1 unit ~ 4 m. sea_draw_local() draws the detail grid alone at a constant depth for the
// staged battle, which is why that scene gets the same water as the map.

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../world/wind.h"
#include "camera.h"
#include "../assets/asset.h"

const rhi_texture_t *terrain_heightmap_texture(void);

static rhi_pipeline_t sea_prg, sea_detail_prg, sea_tess_prg;
static rhi_geometry_t sea_geo, sea_detail_geo, sea_patch_geo, sea_inner_geo;
static int sea_tex_normal = -1, sea_tex_albedo = -1, sea_tex_disp = -1;   // textures.c handles
int sea_tess_on;                 // the tessellated path is in use (GL 4.0 and not MARE_SEA_TESS=0)
int sea_tess_level_max = 32;     // dashboard knob: finest subdivision of a patch edge (zooming in raises wave *height*, not this)

// Tessellated sea (DESIGN_VISUAL.md step 5). Instead of the exponentially warped grid, a
// lattice of SEA_PATCH_N x SEA_PATCH_N quad patches, SEA_PATCH_R units in radius around the
// look-at point and *snapped to a world lattice* of the patch size, so the patches never slide
// with the camera. The control shader picks each edge's subdivision from its projected length
// (a cell of roughly SEA_TESS_PX pixels), the evaluation shader places the surface: heightmap,
// Gerstner waves (with the cell size for the grid-aware fade, exactly as the warped grid did),
// then the authored displacement map (game/assets/sea/sea_displacement.png) riding on top for
// the chop a five-wave sum cannot give. The region is a disc; the world grid cuts a matching
// hole. The warped grid stays as the fallback for GL 3 contexts (sea_tess_on == 0).
#define SEA_PATCH_N   50
#define SEA_PATCH_R   300.f
#define SEA_TESS_PX   11.f     // target screen size of a tessellation cell
// Down at water level even 48 cells per 12-unit patch leaves 0.25-unit facets: pyramids, not
// waves. An inner lattice of small patches covers the disc around the look-at point where the
// camera can get that close; the outer lattice cuts a hole for it. Its rim blends its cell size
// towards the outer lattice's so the grid-aware wave fade agrees across the seam.
#define SEA_INNER_N   32
#define SEA_INNER_R   14.f
int sea_inner_on = 1;          // dashboard / MARE_SEA_INNER=0
// Tessellation earns its cost only where a cell is bigger than a pixel. Past SEA_TESS_DIST the
// warped grid draws the same water for a third of the GPU time, so the patches are a near-camera
// path, not the default (MARE_SEA_TESS=0 turns them off entirely).
#define SEA_TESS_DIST 90.f

#define SEA_DETAIL_N  192        // cells per side of the detail grid (74k triangles)
#define SEA_DETAIL_R  180.f      // world units from the centre to its edge
#define SEA_WARP      4.0f       // cell growth: exp(SEA_WARP * |u|); 0.14 units at the centre, 7.6 at the edge

// The wave set is derived from the wind every frame (sea_update_waves below), not authored.
// shared varyings and the depth lookup both vertex shaders use
#define GLSL_SEA_VS_COMMON \
    "uniform mat4 u_vp; uniform sampler2D u_hm; uniform vec4 u_geo; uniform float u_k;\n" \
    "uniform float u_time; uniform float u_wave_amp; uniform float u_wave_scale; uniform float u_local_depth;\n" \
    "out vec3 v_wpos; out vec3 v_nrm; out float v_slope; out float v_h; out float v_amp; out float v_edge; out float v_jac; out float v_crest;\n" \
    /* shoaling: a wave entering water shallower than its length steepens before it breaks. The \
       boost peaks a little off the beach; the shallows fade still flattens the last metres so \
       nothing breaks through the sand. */ \
    "float shoal_boost(float dep, float scale) { float x = (dep - 0.95 * scale) / (0.5 * scale); return 1.0 + 0.7 * exp(-x * x); }\n" \
    /* raw heightmap value: negative below sea level; > 0.35 is land, which the sea discards */ \
    "float height_at(vec2 p) {\n" \
    "  if (u_local_depth > 0.0) return -u_local_depth;\n" \
    "  float lon = p.x / u_k, lat = -p.y / u_k;\n" \
    "  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));\n" \
    "  return texture(u_hm, uv).r;\n" \
    "}\n"

// world pass: the static grid, flat
static const char *sea_vs =
    "in vec2 a_pos;\n"
    GLSL_SEA_VS_COMMON
    "void main(){\n"
    "  v_wpos = vec3(a_pos.x, 0.0, a_pos.y);\n"
    "  v_nrm = vec3(0.0, 1.0, 0.0); v_slope = 0.0; v_h = height_at(a_pos); v_amp = 0.0; v_edge = 0.0; v_jac = 1.0; v_crest = 0.0;\n"
    "  gl_Position = u_vp * vec4(v_wpos, 1.0);\n"
    "}\n";

// detail pass: a_pos is (u,v) in [-1,1]; the grid is warped around u_center and displaced
static const char *sea_detail_vs =
    "in vec2 a_pos;\n"
    "uniform vec3 u_center;\n"                   // xz = centre, z component unused
    "uniform vec3 u_detail;\n"                   // x = radius, y = warp, z = grid step in a_pos units (2/cells)
    GLSL_SEA_VS_COMMON
    GLSL_WAVES
    "void main(){\n"
    "  vec2 w = sign(a_pos) * (exp(u_detail.y * abs(a_pos)) - 1.0) / (exp(u_detail.y) - 1.0) * u_detail.x;\n"
    "  vec2 p = u_center.xz + w;\n"
    // World size of one cell here: the grid is warped, so cells are ~0.14 units under the
    // camera and ~7.6 at the edge. d|w|/da * the step in a_pos. Waves shorter than a few of
    // these are faded out by wave_sum instead of aliasing into a sliding pattern.
    "  vec2 dwda = u_detail.y * exp(u_detail.y * abs(a_pos)) / (exp(u_detail.y) - 1.0) * u_detail.x;\n"
    "  float cell = max(dwda.x, dwda.y) * u_detail.z;\n"
    "  float h = height_at(p); float dep = max(-h, 0.0);\n"
    // amplitude: full in open water, fading into the shallows and towards the grid edge so the
    // detail grid always meets the flat world grid (and the beach) on a flat surface
    "  float edge = 1.0 - smoothstep(0.55, 0.96, max(abs(a_pos.x), abs(a_pos.y)));\n"
    "  v_edge = edge;\n"
    "  float shal = smoothstep(0.0, 1.1 * u_wave_scale, dep);\n"
    "  float amp = u_wave_amp * edge * shal * shoal_boost(dep, u_wave_scale);\n"
    "  v_amp = amp;\n"
    "  vec3 tx, tz; float crest;\n"
    "  vec3 d = wave_sum(p, amp, u_wave_scale, cell, tx, tz, crest);\n"
    "  v_jac = tx.x * tz.z - tx.z * tz.x; v_crest = crest;\n"
    "  v_slope = 1.0 - normalize(cross(tz, tx)).y;\n"
    "  v_wpos = vec3(p.x + d.x, d.y, p.y + d.z);\n"
    "  v_nrm = normalize(cross(tz, tx));\n"
    "  v_h = h;\n"
    "  gl_Position = u_vp * vec4(v_wpos, 1.0);\n"
    "}\n";

// --- tessellation stages. The vertex stage places lattice corners; the control stage sets the
// levels; the evaluation stage is where the old detail vertex shader's work happens.
static const char *sea_tess_vs =
    "in vec2 a_pos;\n"                       // lattice index (i, j) in [0, N]
    "uniform vec3 u_origin;\n"               // xz of lattice corner (snapped), z component = patch size
    "out vec2 v_lat;\n"
    "void main(){ v_lat = u_origin.xy + a_pos * u_origin.z; }\n";

// Levels are computed in locals and the built-ins are written once and never read back: the
// Intel driver (32.0.101.6733) crashes inside glLinkProgram when a control shader reads
// gl_TessLevelOuter/Inner after writing them (found by bisection; every other construct here
// links). Outer[1] is the edge 0-1, [2] 1-2, [3] 2-3, [0] 3-0 for the (u, v) layout the
// evaluation stage uses.
static const char *sea_tess_tcs =
    "layout(vertices = 4) out;\n"
    "in vec2 v_lat[]; out vec2 tc_pos[]; patch out float tp_cell;\n"
    "uniform vec3 u_campos; uniform vec2 u_tess; uniform float u_patch;\n"   // u_tess: x = pixels per world unit at distance 1 / target px, y = max level
    "float lvl(vec2 a, vec2 b) { vec2 m = (a + b) * 0.5; float d = max(distance(vec3(m.x, 0.0, m.y), u_campos), 1.0); return clamp(u_tess.x * u_patch / d, 1.0, u_tess.y); }\n"
    "void main(){ tc_pos[gl_InvocationID] = v_lat[gl_InvocationID];\n"
    "  if (gl_InvocationID == 0) {\n"
    "    float L0 = lvl(v_lat[3], v_lat[0]), L1 = lvl(v_lat[0], v_lat[1]), L2 = lvl(v_lat[1], v_lat[2]), L3 = lvl(v_lat[2], v_lat[3]);\n"
    "    float I0 = max(L1, L3), I1 = max(L0, L2);\n"
    "    gl_TessLevelOuter[0] = L0; gl_TessLevelOuter[1] = L1; gl_TessLevelOuter[2] = L2; gl_TessLevelOuter[3] = L3;\n"
    "    gl_TessLevelInner[0] = I0; gl_TessLevelInner[1] = I1;\n"
    "    tp_cell = u_patch / max(max(I0, I1), 1.0); } }\n";

#define GLSL_SEA_TES_BODY \
    "layout(quads, fractional_odd_spacing, ccw) in;\n" \
    /* no derivatives in an evaluation stage: every lookup is an explicit LOD 0 (an implicit-LOD \
       texture() here is where the Intel driver crashed at link time) */ \
    "#define texture(s, uv) textureLod(s, uv, 0.0)\n" \
    "in vec2 tc_pos[]; patch in float tp_cell;\n" \
    "uniform vec3 u_center; uniform vec3 u_detail;\n"      /* x = region radius, y = 1 for the inner lattice (no rim fade, seam blend), z = the outer lattice's patch size */ \
    "uniform vec2 u_tess;\n" \
    "uniform sampler2D u_disp; uniform float u_has_disp; uniform vec2 u_drift1; uniform vec2 u_drift2; uniform vec3 u_campos;\n" \
    GLSL_SEA_VS_COMMON \
    GLSL_WAVES \
    "void main(){\n" \
    "  float u = gl_TessCoord.x, v = gl_TessCoord.y;\n" \
    "  vec2 p = mix(mix(tc_pos[0], tc_pos[1], u), mix(tc_pos[3], tc_pos[2], u), v);\n" \
    "  float h = height_at(p); float dep = max(-h, 0.0);\n" \
    "  float r = distance(p, u_center.xz);\n" \
    "  float edge = u_detail.y > 0.5 ? 1.0 : 1.0 - smoothstep(0.78 * u_detail.x, 0.97 * u_detail.x, r);\n" \
    "  float cell = tp_cell;\n" \
    "  if (u_detail.y > 0.5) {\n"   /* inner lattice: towards its rim, pretend to be as coarse as the outer lattice is there */ \
    "    float dcam = max(distance(vec3(p.x, 0.0, p.y), u_campos), 1.0);\n" \
    "    float ocell = u_detail.z / clamp(u_tess.x * u_detail.z / dcam, 1.0, u_tess.y);\n" \
    "    cell = mix(tp_cell, max(ocell, tp_cell), smoothstep(0.55 * u_detail.x, 0.95 * u_detail.x, r));\n" \
    "  }\n" \
    "  float shal = smoothstep(0.0, 1.1 * u_wave_scale, dep);\n" \
    "  float amp = u_wave_amp * edge * shal * shoal_boost(dep, u_wave_scale);\n" \
    "  v_amp = amp; v_edge = edge;\n" \
    "  vec3 tx, tz; float crest;\n" \
    "  vec3 d = wave_sum(p, amp, u_wave_scale, cell, tx, tz, crest);\n" \
    /* whitecaps read the big waves, not the chop: slope, Jacobian and crest come from the \
       Gerstner surface before the displacement map is folded into the tangents */ \
    "  v_slope = 1.0 - normalize(cross(tz, tx)).y;\n" \
    "  v_jac = tx.x * tz.z - tx.z * tz.x; v_crest = crest;\n" \
    /* authored displacement: fine chop on top of the wave sum, two scales drifting downwind, \
       faded where a cell is too big to carry it (the same rule the waves obey) */ \
    "  if (u_has_disp > 0.5 && amp > 0.0) {\n" \
    "    vec2 q = p / u_wave_scale;\n" \
    "    float fine = smoothstep(2.4 * cell, 5.0 * cell, 1.1 * u_wave_scale);\n" \
    "    float dm = (texture(u_disp, q * 0.19 + u_drift1 * 0.8).r - 0.5) * 0.7 + (texture(u_disp, q * 0.90 + u_drift2 * 1.6).r - 0.5) * 0.3 * fine;\n" \
    "    float da = 0.16 * amp * u_wave_scale * smoothstep(1.4 * cell, 3.0 * cell, 5.3 * u_wave_scale);\n" \
    "    d.y += dm * da;\n" \
    "    float e = 0.6 * u_wave_scale;\n"   /* finite-difference slope of the displacement, folded into the tangents */ \
    "    float dmx = (texture(u_disp, (q + vec2(e, 0.0)) * 0.19 + u_drift1 * 0.8).r - texture(u_disp, (q - vec2(e, 0.0)) * 0.19 + u_drift1 * 0.8).r) * 0.7 * da / (2.0 * e);\n" \
    "    float dmz = (texture(u_disp, (q + vec2(0.0, e)) * 0.19 + u_drift1 * 0.8).r - texture(u_disp, (q - vec2(0.0, e)) * 0.19 + u_drift1 * 0.8).r) * 0.7 * da / (2.0 * e);\n" \
    "    tx.y += dmx; tz.y += dmz;\n" \
    "  }\n" \
    "  v_wpos = vec3(p.x + d.x, d.y, p.y + d.z);\n" \
    "  v_nrm = normalize(cross(tz, tx));\n" \
    "  v_h = h;\n" \
    "  gl_Position = u_vp * vec4(v_wpos, 1.0);\n" \
    "}\n"
static const char *sea_tess_tes = GLSL_SEA_TES_BODY;

static const char *sea_fs =
    "in vec3 v_wpos; in vec3 v_nrm; in float v_slope; in float v_h; in float v_amp; in float v_edge; in float v_jac; in float v_crest;\n"
    "uniform sampler2D u_waves; uniform sampler2D u_albedo; uniform float u_has_albedo; uniform sampler2D u_hm;\n"
    "uniform float u_foam;\n"                   // foam strength knob (dashboard / MARE_FOAM), 1 = normal
    "uniform float u_calm;\n"                   // weather: 1 = glassy (mirror-like roughness, more sky in the water)
    "uniform sampler2D u_scene; uniform sampler2D u_scene_depth; uniform vec2 u_scene_size; uniform float u_refract;\n"   // the frame so far, for refraction (hdr_capture_scene); u_refract 0 = alpha blend instead
    "uniform vec3 u_cam_right; uniform vec3 u_cam_up;\n"
    "uniform vec4 u_ships[24]; uniform float u_shiplen[24]; uniform int u_nships;\n"   // nearby ships: x, z, heading, speed; hull length
    "uniform float u_time; uniform float u_wave_scale; uniform float u_local_depth; uniform vec4 u_geo; uniform float u_k;\n"
    "uniform vec3 u_campos; uniform float u_mapness; uniform vec3 u_sky; uniform float u_daylight;\n"
    "uniform vec2 u_drift1; uniform vec2 u_drift2;\n"   // accumulated downwind drift of the two ripple layers (sea_update_waves)
    "uniform vec4 u_hole;\n"                     // xy = centre, z = half size, w = 1 when the world pass must cut the detail square out
    "uniform vec2 u_clip;\n"                     // x = world z the water stops at, y = 1 when that clip is on (staged city: the shore)
    "out vec4 fragColor;\n"
    GLSL_COLOR GLSL_PBR GLSL_LIGHT GLSL_NMAP GLSL_FOG
    "float depth_at(vec2 pp) {\n"              // water depth from the heightmap (staged scenes: constant)
    "  if (u_local_depth > 0.0) return u_local_depth;\n"
    "  float lon = pp.x / u_k, lat = -pp.y / u_k;\n"
    "  vec2 uv = vec2((lon - u_geo.x)/(u_geo.y-u_geo.x), (u_geo.w - lat)/(u_geo.w-u_geo.z));\n"
    "  return max(-texture(u_hm, uv).r, 0.0);\n"
    "}\n"
    "void main(){\n"
    "  if (u_hole.w > 0.5) {\n"                 // the world grid gives way to the detail region: a square for the warped grid, a disc for the patches
    "    if (u_hole.w > 1.5) { if (distance(v_wpos.xz, u_hole.xy) < u_hole.z) discard; }\n"
    "    else if (abs(v_wpos.x - u_hole.x) < u_hole.z && abs(v_wpos.z - u_hole.y) < u_hole.z) discard;\n"
    "  }\n"
    "  if (u_local_depth <= 0.0 && v_h > 0.35) discard;\n"              // land (the same threshold the sea has always used)
    "  if (u_clip.y > 0.5 && v_wpos.z > u_clip.x) discard;\n"           // staged scenes have no heightmap: the shore is a line
    "  float lon = v_wpos.x / u_k, lat = -v_wpos.z / u_k;\n"
    "  float depth = max(-v_h, 0.0);\n"
    "  float dist = distance(v_wpos, u_campos);\n"
    "  float detail = (1.0 - smoothstep(60.0 * u_wave_scale, 400.0 * u_wave_scale, dist)) * v_edge;\n"
    "  vec2 p = v_wpos.xz / u_wave_scale; float t = u_time;\n"
    // two scrolling samples of the tileable wave normal map, large swell + fine chop, riding on
    // top of the Gerstner normal (which carries the shape, this carries the ripple)
    // the ripple detail drifts downwind, like the swell it sits on
    "  vec3 n1 = nmap_decode(texture(u_waves, p * 0.33 + u_drift1).rgb);\n"
    "  vec3 n2 = nmap_decode(texture(u_waves, p * 1.15 + u_drift2 + vec2(0.37, 0.11)).rgb);\n"
    "  vec2 slope = (n1.xy / max(n1.z, 0.3) + n2.xy / max(n2.z, 0.3) * 0.6) * 0.42 * detail;\n"
    "  vec3 wn = normalize(v_nrm);\n"
    "  vec3 n = normalize(wn + vec3(slope.x, 0.0, slope.y));\n"
    "  float w1 = sin(p.x*3.1 + p.y*1.3 + t*1.7);\n"   // keeps the foam band animated
    // North Atlantic, not a lagoon: a blue-grey body colour. The old pair ran to cyan because
    // the shallow end was a tropical green-blue and the absorption killed red hardest.
    "  vec3 deep = vec3(0.035,0.070,0.115), shallow = vec3(0.21,0.285,0.325);\n"
    "  vec3 c = srgb_to_linear(mix(shallow, deep, 1.0 - exp(-depth*0.75)));\n"
    // --- refraction (step 5 follow-up). The scene behind the surface, sampled where the
    // surface normal bends the view; what comes back is absorbed by the water column (red
    // first), so past a few units of depth the seabed - and whether a terrain chunk was drawn
    // there at all - no longer shows. Samples that land on something in front of the water
    // (a hull) fall back to the straight-down sample.
    "  vec3 refr = c; float refr_on = 0.0;\n"
    "  if (u_refract > 0.5 && u_mapness < 0.99) {\n"
    "    vec2 suv = gl_FragCoord.xy / u_scene_size;\n"
    "    vec3 nn = normalize(v_nrm + vec3(slope.x, 0.0, slope.y) * 1.5);\n"
    "    vec2 bend = vec2(dot(nn, u_cam_right), dot(nn, u_cam_up)) * 0.055 * detail / (1.0 + dist * 0.02);\n"
    "    vec2 ruv = clamp(suv + bend, vec2(0.002), vec2(0.998));\n"
    "    float dz = texture(u_scene_depth, ruv).r;\n"
    "    if (dz < gl_FragCoord.z - 1e-5) { ruv = suv; dz = texture(u_scene_depth, ruv).r; }\n"
    "    vec3 bg = texture(u_scene, ruv).rgb;\n"
    // Where no seabed was drawn (terrain chunks with no land are skipped) the frame holds the
    // clear colour, which is sky, not sea floor: the water column's own colour stands in. This
    // is what used to show as a straight-edged lighter wedge over deep water.
    "    if (dz >= 0.99999) bg = c;\n"
    // Absorption with green nearer red than blue: what comes back reads grey-blue rather than
    // teal. The shallower coefficient also lets the bottom show through further out - the
    // transparency the water was missing.
    "    vec3 absorb = vec3(1.55, 1.12, 0.78) / u_wave_scale;\n"
    "    vec3 tr = exp(-absorb * depth * 1.05);\n"
    "    refr = mix(c, bg, tr); refr_on = 1.0;\n"
    "  }\n"
    // authored surface: the foam albedo rides the swell layer (and a finer copy the chop), its
    // bright streaks become whitecaps over the depth colour and its tonal variation breaks up
    // the flat gradient; everything fades out with `detail` like the normals do
    "  vec3 alb1 = texture(u_albedo, p * 0.33 + u_drift1).rgb;\n"
    "  vec3 alb2 = texture(u_albedo, p * 1.15 + u_drift2 + vec2(0.37, 0.11)).rgb;\n"
    "  float lum1 = dot(alb1, vec3(0.3, 0.5, 0.2)), lum2 = dot(alb2, vec3(0.3, 0.5, 0.2));\n"
    "  float streak = max(smoothstep(0.22, 0.58, lum1), 0.5 * smoothstep(0.24, 0.62, lum2)) * u_has_albedo * detail;\n"
    "  c *= mix(1.0, clamp(0.80 + lum1 * 3.0, 0.7, 1.35), 0.30 * u_has_albedo * detail);\n"
    "  vec3 vdir = normalize(u_campos - v_wpos);\n"
    // water: smooth dielectric, so the sun glint comes from the GGX lobe and the sky from Fresnel
    // Specular antialiasing. One pixel covers many waves once the camera pulls back, and a
    // sharp GGX lobe sampled at one point per pixel then turns into a field of fireflies.
    // Measuring how much the normal actually swings per pixel and folding that variance into
    // roughness fixes it at every distance, instead of guessing with a distance curve.
    // Water is a dielectric: almost nothing diffuse. Its colour is what the column transmits
    // (refr) plus the sky it reflects, weighted by Fresnel; the sun adds a glint. The old path
    // lit the body colour as a diffuse surface and read as plastic.
    "  float rough = mix(0.5, 0.26, detail) * mix(1.0, 0.55, u_calm);\n"   // calm air leaves the surface glassy
    "  vec3 ndx = dFdx(n), ndy = dFdy(n);\n"
    "  float nvar = dot(ndx, ndx) + dot(ndy, ndy);\n"
    "  rough = clamp(sqrt(rough*rough + nvar * 2.0), 0.0, 1.0);\n"
    // Fresnel from the wave normal with only a touch of the ripple detail: per-ripple Fresnel
    // at a grazing angle turns the whole sea into sky-coloured chalk. Capped so the body colour
    // still reads at water level, where a real sea is dark glass, not a mirror of the haze.
    "  vec3 nf = normalize(wn + vec3(slope.x, 0.0, slope.y) * 0.25);\n"
    "  float fres = 0.02 + 0.98 * pow(1.0 - max(dot(nf, vdir), 0.0), 5.0);\n"
    "  fres = min(fres, 0.58 + 0.2 * u_calm);\n"
    // Dispersion: the three channels leave the surface at slightly different angles, so a
    // grazing reflection is faintly cool at the edge and warm inside it. A single achromatic
    // lobe is what makes water read as moulded plastic.
    "  vec3 fres3 = clamp(vec3(fres) * vec3(0.93, 1.0, 1.09), 0.0, 1.0);\n"
    "  vec3 skyrefl = srgb_to_linear(u_sky) * (0.40 + 0.4 * u_calm) + u_ambsky * 0.32;\n"
    "  vec3 glint = sun_shade(vec3(0.0), n, vdir, v_wpos, rough, 0.0, dist) * 0.26;\n"   // albedo 0: the specular lobe alone, in the sun's shadow where it should be
    // Sparkle: the sub-ripple normals the shading normal has already averaged away, put back as
    // a narrow glitter. A real sea's highlight is thousands of points, not one smooth patch.
    "  vec3 nsp = normalize(wn + vec3(slope.x, 0.0, slope.y) * 2.6);\n"
    "  float sp = pow(max(dot(nsp, normalize(u_sun + vdir)), 0.0), 190.0);\n"
    "  glint += u_suncol * sp * 0.22 * detail * (1.0 - 0.6 * u_calm);\n"
    "  vec3 body = sun_shade(c, n, vdir, v_wpos, 0.9, 0.0, dist);\n"                   // the little diffuse the suspended sediment gives
    "  vec3 col3d = mix(mix(body, refr * (0.62 + 0.45 * u_ambsky.g * 3.0), refr_on), skyrefl, fres3) + glint;\n"
    // --- foam (DESIGN_VISUAL.md step 6). Four sources, one white.
    // Whitecaps: where the Gerstner displacement folds the surface - the horizontal Jacobian
    // drops below 1 as the crest compresses - plus the steepest faces. Textured by the foam
    // albedo so caps are patchy, not painted. Only the big waves count (v_jac/v_slope come from
    // the wave sum, before the displacement map), and the amplitude gate keeps a calm sea bare.
    "  float sc = u_wave_scale;\n"
    "  float ws = clamp(v_amp, 0.0, 1.4);\n"
    "  float fold = smoothstep(0.93 - 0.05 * ws, 0.74 - 0.05 * ws, v_jac);\n"
    "  float steep = smoothstep(0.04, 0.10, v_slope) * 0.7;\n"
    "  float caps = max(fold, steep) * smoothstep(0.25, 0.8, ws) * (0.5 + 1.0 * lum2) * detail;\n"
    // Surf: in the shoaling band the steepened waves break into a foam sheet that runs up the
    // beach. The shore direction is the depth gradient; the sheet's texture scrolls along it.
    "  vec2 hp = v_wpos.xz; float he = 0.9 * sc;\n"
    "  vec2 g = vec2(depth_at(hp + vec2(he, 0.0)) - depth_at(hp - vec2(he, 0.0)), depth_at(hp + vec2(0.0, he)) - depth_at(hp - vec2(0.0, he)));\n"
    "  vec2 shoredir = length(g) > 1e-4 ? -normalize(g) : vec2(0.0);\n"
    "  float band = smoothstep(1.8 * sc, 0.5 * sc, depth) * smoothstep(0.0, 0.16 * sc, depth);\n"
    "  float surf_tex = smoothstep(0.22, 0.6, dot(texture(u_albedo, p * 0.7 + shoredir * (t * 0.22) + u_drift1 * 0.5).rgb, vec3(0.3, 0.5, 0.2)));\n"
    "  float surf = band * (0.3 + 0.7 * max(v_crest, fold)) * (0.45 + 0.75 * surf_tex) * smoothstep(0.1, 0.5, ws + 0.3) * detail;\n"
    // Shoreline: the thin lapping line at the waterline, as before.
    "  float foam = (1.0 - smoothstep(0.0, 0.45 * sc, depth)) * (0.55 + 0.45*sin(depth*30.0/sc - t*2.0 + w1)) * detail;\n"
    // Ships: a bow wave, a thin line along the hull and a V wake that widens and thins astern,
    // all scaling with speed; a running sea throws more white off the bow.
    "  float shipk = 1.0;\n"                  // 1 - union of the ships' foam: overlapping wakes never add up to a sheet
    "  for (int i = 0; i < u_nships; ++i) {\n"
    "    vec2 dxz = v_wpos.xz - u_ships[i].xy; float L = max(u_shiplen[i], 0.2);\n"
    "    if (dot(dxz, dxz) > 25.0 * L * L) continue;\n"
    "    float ch = cos(u_ships[i].z), sh = -sin(u_ships[i].z);\n"          // forward = (cos h, -sin h), as scene.c
    "    float along = dxz.x * ch + dxz.y * sh, across = abs(-dxz.x * sh + dxz.y * ch);\n"
    "    float spd = clamp(u_ships[i].w / 5.0, 0.15, 1.0);\n"
    "    float bowx = (along - 0.42 * L) / (0.22 * L), bowz = across / (0.24 * L);\n"
    "    float bow = exp(-(bowx * bowx + bowz * bowz)) * (0.45 + 0.45 * min(ws, 1.0)) * spd;\n"
    "    float hull = smoothstep(-0.5 * L, -0.42 * L, along) * (1.0 - smoothstep(0.36 * L, 0.44 * L, along)) * smoothstep(0.17 * L, 0.20 * L, across) * (1.0 - smoothstep(0.22 * L, 0.26 * L, across)) * 0.35 * spd;\n"
    "    float back = -along - 0.4 * L;\n"
    "    float ww = 0.18 * L + 0.24 * max(back, 0.0);\n"
    "    float wake = step(0.0, back) * (1.0 - smoothstep(ww * 0.7, ww, across)) * exp(-back / (2.5 * L * spd + 0.3 * L)) * spd;\n"
    "    wake *= (0.25 + 0.55 * lum2 + 0.5 * smoothstep(ww * 0.55, ww * 0.9, across)) * 0.55;\n"   // the V edges foam most
    "    shipk *= 1.0 - clamp(bow + hull + wake, 0.0, 0.85);\n"
    "  }\n"
    "  float shipf = (1.0 - shipk) * detail;\n"
    "  vec3 foamcol = srgb_to_linear(vec3(0.93, 0.96, 0.98)) * (u_ambsky * 1.0 + u_suncol * 0.35);\n"
    "  float foamall = clamp((caps * 0.9 + surf * 0.8 + shipf + foam * 0.7) * u_foam, 0.0, 1.0);\n"
    "  col3d = mix(col3d, foamcol, foamall);\n"
    "  col3d = mix(col3d, srgb_to_linear(vec3(0.90,0.93,0.95)) * (u_ambsky * 0.9 + u_suncol * 0.30), streak * 0.10 * u_foam);\n"
    "  float caps_a = foamall;\n"
    "  vec3 mapc = mix(vec3(0.62,0.72,0.80), vec3(0.50,0.62,0.74), 1.0 - exp(-depth*0.8));\n"
    "  float gx = abs(fract(lon/10.0 + 0.5) - 0.5) * 10.0, gy = abs(fract(lat/10.0 + 0.5) - 0.5) * 10.0;\n"
    "  float grid = 1.0 - smoothstep(0.0, 0.06, min(gx, gy));\n"
    "  float gx5 = abs(fract(lon/5.0 + 0.5) - 0.5) * 5.0, gy5 = abs(fract(lat/5.0 + 0.5) - 0.5) * 5.0;\n"
    "  float grid5 = (1.0 - smoothstep(0.0, 0.035, min(gx5, gy5))) * 0.5;\n"
    "  mapc = mix(mapc, vec3(0.35,0.42,0.52), max(grid, grid5) * 0.8);\n"
    "  mapc = mix(mapc, vec3(0.30,0.40,0.52), (1.0 - smoothstep(0.0, 0.6, depth))*0.55);\n"
    "  vec3 col = fog_apply(mix(col3d, srgb_to_linear(mapc) * u_daylight, u_mapness), dist, u_mapness, srgb_to_linear(u_sky));\n"
    // transparency: the terrain below has already been drawn, so shallow water reveals the
    // seabed and deep water goes opaque; the map view stays solid parchment
    "  float alpha = mix(0.38, 0.92, smoothstep(0.0, 1.6 * u_wave_scale, depth));\n"   // clearer shallows, seabed reads through a little further out
    "  alpha = max(alpha, caps_a * 0.9);\n"        // foam stays visible over the seabed
    "  alpha = mix(alpha, 1.0, max(u_mapness, refr_on));\n"   // refraction composites the background itself: opaque
    "  fragColor = vec4(col, alpha);\n"
    "}\n";

static const char *sea_uniforms[] = { GLSL_WAVE_UNIFORMS, "u_drift1", "u_drift2", "u_origin", "u_patch", "u_tess", "u_disp", "u_has_disp", "u_foam", "u_calm", "u_ships", "u_shiplen", "u_nships", "u_scene", "u_scene_depth", "u_scene_size", "u_refract", "u_cam_right", "u_cam_up", "u_vp", "u_hm", "u_waves", "u_albedo", "u_has_albedo", "u_geo", "u_k", "u_time",
                                      "u_campos", "u_mapness", "u_sky", "u_daylight", "u_center", "u_detail",
                                      "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hole", "u_clip", GLSL_LIGHT_UNIFORMS, 0 };

void sea_init(void) {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    sea_prg        = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sea_vs,        .fs = sea_fs, .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA });
    sea_detail_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sea_detail_vs, .fs = sea_fs, .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA });
    sea_tess_on = rhi_tessellation_supported();
    { const char *e = getenv("MARE_SEA_TESS"); if (e && !atoi(e)) sea_tess_on = 0; }
    if (sea_tess_on) {
        sea_tess_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = sea_tess_vs, .tcs = sea_tess_tcs, .tes = sea_tess_tes, .fs = sea_fs, .patch_verts = 4,
                                                                 .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA });
        // patch lattices: (i, j) indices, four corners per patch counter-clockwise seen from above
        for (int which = 0; which < 2; ++which) {
            int n = which ? SEA_INNER_N : SEA_PATCH_N, vn = n + 1;
            float *v = malloc((size_t)vn*vn*2*sizeof(float));
            for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[(y*vn+x)*2] = (float)x; v[(y*vn+x)*2+1] = (float)y; }
            unsigned *idx = malloc((size_t)n*n*4*sizeof(unsigned)); int m = 0;
            for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { unsigned a = y*vn+x; idx[m++] = a; idx[m++] = a+1; idx[m++] = a+vn+1; idx[m++] = a+vn; }
            *(which ? &sea_inner_geo : &sea_patch_geo) = rhi_geometry_make(&layout, v, vn*vn, idx, m);
            free(v); free(idx);
        }
        LOG_INFO("gpu", "sea: tessellated patches (%dx%d, radius %.0f, cells ~%.0f px, up to %d per edge), displacement map on", SEA_PATCH_N, SEA_PATCH_N, SEA_PATCH_R, SEA_TESS_PX, sea_tess_level_max);
    } else LOG_INFO("gpu", "sea: warped grid (no tessellation on this context)");
    {   // world grid: static, covers the whole map
        int nx = 88, ny = 65;
        float *v = malloc((nx+1)*(ny+1)*2*sizeof(float));
        float x0 = lon2x(LON0), x1 = lon2x(LON1), z0 = lat2z(LAT1), z1 = lat2z(LAT0);
        for (int y = 0; y <= ny; ++y) for (int x = 0; x <= nx; ++x) { v[(y*(nx+1)+x)*2] = lerpf(x0,x1,x/(float)nx); v[(y*(nx+1)+x)*2+1] = lerpf(z0,z1,y/(float)ny); }
        unsigned *idx = malloc(nx*ny*6*sizeof(unsigned)); int m = 0;
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) { unsigned a = y*(nx+1)+x, b = a+1, c = a+nx+1, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
        sea_geo = rhi_geometry_make(&layout, v, (nx+1)*(ny+1), idx, m);
        free(v); free(idx);
    }
    {   // detail grid: parameter space [-1,1]^2, warped and placed by the vertex shader
        int n = SEA_DETAIL_N, vn = n + 1;
        float *v = malloc((size_t)vn*vn*2*sizeof(float));
        for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[(y*vn+x)*2] = x * 2.f / n - 1.f; v[(y*vn+x)*2+1] = y * 2.f / n - 1.f; }
        unsigned *idx = malloc((size_t)n*n*6*sizeof(unsigned)); int m = 0;
        for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { unsigned a = y*vn+x, b = a+1, c = a+vn, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
        sea_detail_geo = rhi_geometry_make(&layout, v, vn*vn, idx, m);
        free(v); free(idx);
    }
    // authored tiles, streamed (no stall at start-up; the procedural map covers until they land)
    char path[600];
    snprintf(path, sizeof path, "%s/sea/sea_normal.png", asset_root()); sea_tex_normal = texture_request(path, RHI_FMT_RGB8, RHI_FILTER_MIPMAP, RHI_WRAP_REPEAT);
    snprintf(path, sizeof path, "%s/sea/sea_albedo.png", asset_root()); sea_tex_albedo = texture_request(path, RHI_FMT_SRGB8, RHI_FILTER_MIPMAP, RHI_WRAP_REPEAT);
    snprintf(path, sizeof path, "%s/sea/sea_displacement.png", asset_root()); sea_tex_disp = texture_request(path, RHI_FMT_RGB8, RHI_FILTER_MIPMAP, RHI_WRAP_REPEAT);
}

// ---------------------------------------------------------------------------
// The wave set (DESIGN_VISUAL.md step 4)
//
// Five Gerstner waves generated from the wind at the sea's centre (world/wind.c), not authored:
//
//   direction   spread around the wind direction; the spread narrows as the wind strengthens,
//               so a gale runs in near-parallel lines and a light air is confused and crossed
//   wavelength  a geometric series whose longest member grows with the wind, so a fresh breeze
//               makes a longer, slower swell as well as a taller one
//   height      amplitude follows wavelength (WAVE_STEEP caps the steepness) and the whole
//               field scales with the wind: at the edge of the doldrums the sea is nearly glassy
//   phase       accumulated per wave (see GLSL_WAVES); each wave also gets a fixed random
//               offset from the game seed, so two games do not get the same sea
//
// A set, once built, is frozen: its directions and wavelengths never change while it is on
// screen (GLSL_WAVES explains why - a rotating direction is crest motion). When the wind at the
// look-at point has moved more than SEA_RESET_ANGLE or changed strength by SEA_RESET_STR since
// the current set was built, a second set is built from the new wind and the two are
// cross-faded by amplitude over SEA_BLEND_S. The ripple textures drift by an *accumulated*
// offset for the same reason: `direction * time` would jump when the direction changed.
#define SEA_BLEND_S       6.0f    // seconds a set change takes to cross-fade
#define SEA_RESET_ANGLE   0.21f   // radians of wind swing that calls for a new set (~12 degrees)
#define SEA_RESET_STR     0.22f   // wind strength change (absolute) that calls for a new set
#define SEA_SPREAD_MAX    0.95f   // radians either side of the wind in a light air
#define SEA_SPREAD_MIN    0.28f   // ... and in a strong one
#define SEA_LEN0          3.6f    // longest wavelength at wind strength 1, world units
#define SEA_LEN_RATIO     0.64f   // each wave shorter than the last by this factor

typedef struct sea_wave_t { float dir[2], len, steep; } sea_wave_t;
typedef struct sea_set_t { sea_wave_t w[SEA_WAVES]; float phase[SEA_WAVES]; float wdx, wdz, str, amp; int valid; } sea_set_t;
static sea_set_t sea_set[2];                   // [0] current, [1] the one fading in
static float sea_blend;                        // 0 = set 0 alone .. 1 = set 1 alone
static float sea_wave_jit[SEA_WAVES][4];       // per-wave randomness: angle, length, steepness, phase offset
static int   sea_wave_seeded;
static float sea_drift1[2], sea_drift2[2];     // accumulated texture offsets of the two ripple layers
int   sea_set_changes;                         // sets built this run (dashboard, log)
float sea_wind_dx = 1.f, sea_wind_dz = 0.f, sea_wind_str = 1.f;   // the wind the current set was built from (dashboard)
float sea_wind_amp = 1.f;                                         // wave height factor of the current set
float sea_wind_now_str = 1.f;                                     // the wind right now at the look-at point (weather)

static float sea_hash01(unsigned h) {
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (h & 0xFFFFFF) / (float)0x1000000;
}

static float sea_amp_for(float str) {
    float s = clampf(str, 0.f, 1.6f);
    // Height: the doldrums (strength ~0.35) leave a nearly flat sea, a strong westerly a heavy one.
    return clampf(0.10f + 0.95f * (s - 0.18f), 0.06f, 1.55f);
}

static void sea_set_build(sea_set_t *set, float wdx, float wdz, float str) {
    float s = clampf(str, 0.f, 1.6f);
    float spread = lerpf(SEA_SPREAD_MAX, SEA_SPREAD_MIN, clampf((s - 0.3f) / 0.9f, 0.f, 1.f));
    float base = atan2f(wdz, wdx);
    float len0 = SEA_LEN0 * (0.70f + 0.45f * s);
    for (int i = 0; i < SEA_WAVES; ++i) {
        float t = (float)i / (SEA_WAVES - 1) * 2.f - 1.f;                  // -1 .. +1 across the fan
        float ang = base + (t * 0.8f + sea_wave_jit[i][0] * 0.7f) * spread;
        set->w[i].dir[0] = cosf(ang); set->w[i].dir[1] = sinf(ang);
        set->w[i].len = len0 * powf(SEA_LEN_RATIO, (float)i) * sea_wave_jit[i][1];
        // the long leading swell carries the most height; the short cross seas ride on it
        set->w[i].steep = (0.62f - 0.07f * i) * sea_wave_jit[i][2];
        set->phase[i] = sea_wave_jit[i][3];
    }
    set->wdx = wdx; set->wdz = wdz; set->str = str; set->amp = sea_amp_for(str); set->valid = 1;
}

// Once per frame, before anything draws water. `day` drives the wind model, `dt` the clocks.
void sea_update_waves(float day, float dt) {
    if (!sea_wave_seeded) {
        sea_wave_seeded = 1;
        for (int i = 0; i < SEA_WAVES; ++i) {
            unsigned h = G.seed * 0x9E3779B1u + (unsigned)i * 0x85EBCA6Bu;
            sea_wave_jit[i][0] = sea_hash01(h) - 0.5f;                     // angle offset, +-0.5 of the band
            sea_wave_jit[i][1] = 0.78f + 0.44f * sea_hash01(h ^ 0x51u);    // wavelength multiplier
            sea_wave_jit[i][2] = 0.80f + 0.40f * sea_hash01(h ^ 0xA3u);    // steepness multiplier
            sea_wave_jit[i][3] = sea_hash01(h ^ 0xF7u) * 6.2831853f;       // so they do not all crest together at t=0
        }
    }
    float wdx, wdz, str;
    wind_at(RF.sea_center.x, RF.sea_center.z, day, &wdx, &wdz, &str);
    sea_wind_now_str = str;
    if (!sea_set[0].valid) { sea_set_build(&sea_set[0], wdx, wdz, str); sea_blend = 0; sea_set_changes++; }
    // Has the wind at the look-at point left the band the current set was built for? Only ask
    // while no change is in progress: a pan across several wind cells produces a few discrete
    // cross-fades, never a continuous drift.
    if (!sea_set[1].valid) {
        const sea_set_t *cur = &sea_set[0];
        float dot = clampf(wdx * cur->wdx + wdz * cur->wdz, -1.f, 1.f);
        if (acosf(dot) > SEA_RESET_ANGLE || fabsf(str - cur->str) > SEA_RESET_STR) {
            sea_set_build(&sea_set[1], wdx, wdz, str); sea_blend = 0; sea_set_changes++;
            LOG_DEBUG("gfx", "sea: new wave set %d (wind %.0f deg strength %.2f -> %.0f deg %.2f), cross-fading over %.0f s",
                      sea_set_changes, atan2f(cur->wdz, cur->wdx) * 57.3f, cur->str, atan2f(wdz, wdx) * 57.3f, str, SEA_BLEND_S);
        }
    }
    if (sea_set[1].valid) {
        sea_blend = minf(1.f, sea_blend + dt / SEA_BLEND_S);
        if (sea_blend >= 1.f) { sea_set[0] = sea_set[1]; sea_set[1].valid = 0; sea_blend = 0; }
    }
    // clocks: each wave advances at its own deep-water speed; the ripple layers drift downwind
    for (int k = 0; k < 2; ++k) {
        sea_set_t *set = &sea_set[k]; if (!set->valid) continue;
        for (int i = 0; i < SEA_WAVES; ++i) {
            float kw = 6.2831853f / maxf(set->w[i].len, 1e-4f);
            float spd = sqrtf(9.81f / kw) * 0.42f;
            set->phase[i] += kw * spd * dt;
            if (set->phase[i] > 6.2831853f) set->phase[i] -= 6.2831853f * floorf(set->phase[i] / 6.2831853f);
        }
    }
    const sea_set_t *shown = sea_set[1].valid && sea_blend > 0.5f ? &sea_set[1] : &sea_set[0];
    sea_wind_dx = shown->wdx; sea_wind_dz = shown->wdz; sea_wind_str = shown->str;
    sea_wind_amp = lerpf(sea_set[0].amp, sea_set[1].valid ? sea_set[1].amp : sea_set[0].amp, sea_blend);
    // the drift follows the *shown* set's wind, so the texture direction changes only with a set
    // change - and as an offset, so even then nothing jumps
    sea_drift1[0] += shown->wdx * 0.030f * dt; sea_drift1[1] += shown->wdz * 0.030f * dt;
    float sx = shown->wdx * 0.91f - shown->wdz * 0.42f, sz = shown->wdx * 0.42f + shown->wdz * 0.91f;   // 25 degrees off the wind
    sea_drift2[0] += sx * 0.058f * dt; sea_drift2[1] += sz * 0.058f * dt;
}

// Height of the wave surface at a world point, the same sum the shaders evaluate (no shallows
// or rim fade: the camera clamp wants the worst case). Used to keep the eye above the water.
float sea_height_at(float x, float z) {
    float amp = sea_wave_amp * sea_wind_amp * (1.f - RF.mapness) * sea_zoom_amp(), h = 0.f;
    if (amp <= 0.f) return 0.f;
    for (int k = 0; k < 2; ++k) {
        const sea_set_t *set = &sea_set[k]; if (!set->valid) continue;
        float setw = k == 0 ? 1.f - (sea_set[1].valid ? sea_blend : 0.f) : sea_blend;
        for (int i = 0; i < SEA_WAVES; ++i) {
            float len = set->w[i].len, kw = 6.2831853f / maxf(len, 1e-4f);
            float a = set->w[i].steep * setw * amp * len * 0.030f;
            h += a * sinf(kw * (set->w[i].dir[0] * x + set->w[i].dir[1] * z) - set->phase[i]);
        }
    }
    return h + 0.16f * amp * 0.5f;    // plus the displacement map's reach
}

// Upload the wave set to any pipeline built with GLSL_WAVES (the sea and the instanced meshes,
// so ships ride exactly the water they float on).
void sea_bind_waves(const rhi_pipeline_t *p) {
    float w[SEA_WAVE_SLOTS * 4], ph[SEA_WAVE_SLOTS];
    for (int k = 0; k < 2; ++k) for (int i = 0; i < SEA_WAVES; ++i) {
        const sea_set_t *set = sea_set[k].valid ? &sea_set[k] : &sea_set[0];
        int n = k * SEA_WAVES + i;
        w[n*4+0] = set->w[i].dir[0]; w[n*4+1] = set->w[i].dir[1]; w[n*4+2] = set->w[i].len; w[n*4+3] = set->w[i].steep;
        ph[n] = set->phase[i];
    }
    rhi_set_f4v(rhi_uniform(p, "u_wave"), w, SEA_WAVE_SLOTS);
    rhi_set_f1v(rhi_uniform(p, "u_wave_phase"), ph, SEA_WAVE_SLOTS);
    rhi_set_f1(rhi_uniform(p, "u_wave_blend"), sea_set[1].valid ? sea_blend : 0.f);
    rhi_set_f2(rhi_uniform(p, "u_drift1"), sea_drift1[0], sea_drift1[1]);
    rhi_set_f2(rhi_uniform(p, "u_drift2"), sea_drift2[0], sea_drift2[1]);
}

int sea_nyquist = 1;         // fade waves the detail grid cannot sample (MARE_SEA_NYQUIST=0 shows the aliasing it removes)
float sea_foam = 1.f;        // foam strength (dashboard / MARE_FOAM; 0 = none)
int   sea_refract = 1;       // refract the frame behind the water (MARE_SEA_REFRACT=0: the old alpha blend)
#define SEA_MAX_SHIPS 24
static float sea_ships[SEA_MAX_SHIPS * 4], sea_ship_len[SEA_MAX_SHIPS]; static int sea_nships;
// Ships the water should foam around this frame: x, z, heading, speed (world units/s) and hull
// length. scene.c gives the fleets near the look-at point, detailview.c its staged ships.
void sea_set_ships(const float *xzhs, const float *len, int n) {
    sea_nships = n > SEA_MAX_SHIPS ? SEA_MAX_SHIPS : n;
    if (sea_nships > 0) { memcpy(sea_ships, xzhs, sea_nships * 4 * sizeof(float)); memcpy(sea_ship_len, len, sea_nships * sizeof(float)); }
}
float sea_wave_amp = 1.f;    // global wave height knob (dashboard / MARE_WAVES; 0 = flat water)
float sea_wave_scale = 1.f;  // set per scene; the instanced meshes read it too, so ships ride the same swell
float sea_local_depth = 0.f;
float sea_clip_z; int sea_clip_on;   // staged scenes: water only on the seaward side of this world z

// uniforms every sea draw shares
static void sea_bind_common(const rhi_pipeline_t *p, float wave_scale, float local_depth) {
    sea_bind_waves(p);
    rhi_set_mat4(rhi_uniform(p,"u_vp"), &RF.vp.x.x);
    rhi_set_i1(rhi_uniform(p,"u_hm"), 0); rhi_texture_bind(0, terrain_heightmap_texture());
    const rhi_texture_t *nrm = texture_get(sea_tex_normal), *alb = texture_get(sea_tex_albedo);
    rhi_set_i1(rhi_uniform(p,"u_waves"), 1); rhi_texture_bind(1, nrm ? nrm : detail_waves());
    rhi_set_i1(rhi_uniform(p,"u_albedo"), 2); rhi_texture_bind(2, alb ? alb : detail_waves());
    rhi_set_f1(rhi_uniform(p,"u_has_albedo"), alb ? 1.f : 0.f);
    rhi_set_f1(rhi_uniform(p,"u_foam"), sea_foam);
    rhi_set_f1(rhi_uniform(p,"u_calm"), RF.calm);
    { const rhi_texture_t *sc = hdr_scene_copy(), *dp = hdr_depth();
      int on = sea_refract && sc && dp;
      rhi_set_f1(rhi_uniform(p,"u_refract"), on ? 1.f : 0.f);
      if (on) { rhi_set_i1(rhi_uniform(p,"u_scene"), 6); rhi_texture_bind(6, sc);
                rhi_set_i1(rhi_uniform(p,"u_scene_depth"), 7); rhi_texture_bind(7, dp);
                rhi_set_f2(rhi_uniform(p,"u_scene_size"), (float)sc->w, (float)sc->h); }
      rhi_set_f3(rhi_uniform(p,"u_cam_right"), cam.right.x, cam.right.y, cam.right.z);
      rhi_set_f3(rhi_uniform(p,"u_cam_up"), cam.up.x, cam.up.y, cam.up.z); }
    rhi_set_i1(rhi_uniform(p,"u_nships"), sea_nships);
    if (sea_nships) { rhi_set_f4v(rhi_uniform(p,"u_ships"), sea_ships, sea_nships); rhi_set_f1v(rhi_uniform(p,"u_shiplen"), sea_ship_len, sea_nships); }
    { const rhi_texture_t *dsp = texture_get(sea_tex_disp);
      rhi_set_i1(rhi_uniform(p,"u_disp"), 5); rhi_texture_bind(5, dsp ? dsp : detail_waves());
      rhi_set_f1(rhi_uniform(p,"u_has_disp"), dsp ? 1.f : 0.f); }
    rhi_set_f4(rhi_uniform(p,"u_geo"), LON0, LON1, LAT0, LAT1);
    rhi_set_f1(rhi_uniform(p,"u_k"), WORLD_K);
    rhi_set_f1(rhi_uniform(p,"u_time"), RF.time);
    rhi_set_f3(rhi_uniform(p,"u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(rhi_uniform(p,"u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(rhi_uniform(p,"u_mapness"), RF.mapness);
    rhi_set_f1(rhi_uniform(p,"u_daylight"), RF.daylight);
    rhi_set_f1(rhi_uniform(p,"u_wave_scale"), wave_scale);
    rhi_set_f1(rhi_uniform(p,"u_local_depth"), local_depth);
    rhi_set_f2(rhi_uniform(p,"u_clip"), sea_clip_z, sea_clip_on ? 1.f : 0.f);
    light_bind(p);
}

// Wave height follows the zoom (DESIGN_STREAMING.md task 6): full displacement only in the
// city/battle view, flattening steadily as the camera pulls out - the tessellated sea keeps
// its cell density but loses height, which is what reads as distance instead of chop.
float sea_zoom_amp(void) { return clampf(1.15f - cam.dist / 200.f, 0.35f, 1.f); }

// how tall the waves may be right now: they flatten as the view turns into a map and with the
// zoom, and the detail grid only exists while the camera is close enough for it to matter
float sea_amp_now(void) { return sea_wave_amp * sea_wind_amp * (1.f - RF.mapness) * sea_zoom_amp(); }

// The tessellated detail region: patches on a world lattice around the look-at point.
// `radius` and `scale` as for the warped grid. Shared by the map and the staged scenes.
static void sea_draw_lattice(float cx, float cz, float radius, int n, float scale, float local_depth, float amp, int inner, float outer_patch, float hole_r) {
    float patch = 2.f * radius / n;
    float ox = floorf((cx - radius) / patch) * patch, oz = floorf((cz - radius) / patch) * patch;   // snapped, so patches never slide
    rhi_pipeline_bind(&sea_tess_prg);
    sea_bind_common(&sea_tess_prg, scale, local_depth);
    rhi_set_f3(rhi_uniform(&sea_tess_prg,"u_origin"), ox, oz, patch);
    rhi_set_f1(rhi_uniform(&sea_tess_prg,"u_patch"), patch);
    rhi_set_f3(rhi_uniform(&sea_tess_prg,"u_center"), cx, 0, cz);
    rhi_set_f3(rhi_uniform(&sea_tess_prg,"u_detail"), radius, inner ? 1.f : 0.f, outer_patch);
    float kpx = screen_h / (2.f * tanf(45.f * 0.0174533f * 0.5f));            // pixels per world unit at distance 1 (camera.c FOV)
    rhi_set_f2(rhi_uniform(&sea_tess_prg,"u_tess"), kpx / SEA_TESS_PX, (float)sea_tess_level_max);
    rhi_set_f1(rhi_uniform(&sea_tess_prg,"u_wave_amp"), amp);
    rhi_set_f4(rhi_uniform(&sea_tess_prg,"u_hole"), cx, cz, hole_r, hole_r > 0 ? 2.f : 0.f);
    rhi_draw(n == SEA_PATCH_N ? &sea_patch_geo : &sea_inner_geo, 0);
}
static void sea_draw_patches(float cx, float cz, float radius, float scale, float local_depth, float amp) {
    float outer_patch = 2.f * radius / SEA_PATCH_N, inner_r = SEA_INNER_R * scale;
    int inner = sea_inner_on && cam.dist < 60.f * scale;      // only when the camera can be close to the water
    sea_draw_lattice(cx, cz, radius, SEA_PATCH_N, scale, local_depth, amp, 0, outer_patch, inner ? inner_r * 0.985f : 0.f);
    if (inner) sea_draw_lattice(cx, cz, inner_r, SEA_INNER_N, scale, local_depth, amp, 1, outer_patch, 0.f);
}

int sea_tess_now;    // the tessellated path is in use this frame (dashboard)

void sea_draw(void) {
    float amp = sea_amp_now();
    int detail = amp > 0.001f && RF.mapness < 0.995f;
    float cx = RF.sea_center.x, cz = RF.sea_center.z;
    sea_tess_now = sea_tess_on && cam.dist < SEA_TESS_DIST;
    float hole_r = sea_tess_now ? SEA_PATCH_R * 0.985f : SEA_DETAIL_R * 0.98f;
    rhi_pipeline_bind(&sea_prg);
    sea_bind_common(&sea_prg, 1.f, 0.f);
    rhi_set_f1(rhi_uniform(&sea_prg,"u_wave_amp"), 0.f);
    rhi_set_f4(rhi_uniform(&sea_prg,"u_hole"), cx, cz, detail ? hole_r : 0.f, detail ? (sea_tess_now ? 2.f : 1.f) : 0.f);
    rhi_draw(&sea_geo, 0);
    if (!detail) return;
    if (sea_tess_now) { sea_draw_patches(cx, cz, SEA_PATCH_R, 1.f, 0.f, amp); return; }
    rhi_pipeline_bind(&sea_detail_prg);
    sea_bind_common(&sea_detail_prg, 1.f, 0.f);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_center"), cx, 0, cz);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_detail"), SEA_DETAIL_R, SEA_WARP, sea_nyquist ? 2.f / SEA_DETAIL_N : 0.f);
    rhi_set_f1(rhi_uniform(&sea_detail_prg,"u_wave_amp"), amp);
    rhi_set_f4(rhi_uniform(&sea_detail_prg,"u_hole"), 0, 0, 0, 0);
    rhi_draw(&sea_detail_geo, 0);
}

// Staged scenes (detailview.c): the detail grid alone, centred on the camera, at a constant
// depth and with no land to discard. `scale` stretches the wave field to the scene's units.
// `clip_z` (when `clip_on`) is the shoreline: a staged scene has no heightmap, so the water's
// landward limit is given as a plane rather than discovered from the terrain.
void sea_draw_local(float depth, float scale, float radius) {
    sea_tess_now = sea_tess_on && cam.dist < SEA_TESS_DIST * scale;
    if (sea_tess_now) { sea_draw_patches(RF.sea_center.x, RF.sea_center.z, radius, scale, depth, sea_wave_amp * sea_wind_amp * sea_zoom_amp()); return; }
    rhi_pipeline_bind(&sea_detail_prg);
    sea_bind_common(&sea_detail_prg, scale, depth);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_center"), RF.sea_center.x, 0, RF.sea_center.z);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_detail"), radius, SEA_WARP, sea_nyquist ? 2.f / SEA_DETAIL_N : 0.f);
    rhi_set_f1(rhi_uniform(&sea_detail_prg,"u_wave_amp"), sea_wave_amp * sea_wind_amp * sea_zoom_amp());
    rhi_set_f4(rhi_uniform(&sea_detail_prg,"u_hole"), 0, 0, 0, 0);
    rhi_draw(&sea_detail_geo, 0);
}
