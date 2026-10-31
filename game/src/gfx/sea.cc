// mare::engine::render::Sea (see render.h): a camera-centred detail grid with Gerstner waves
// over a world-wide flat plane, shaded
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

#include "../platform/v3cpp.hh"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>

extern "C" {
#include "render.h"
#include "glsl.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../world/wind.h"
#include "camera.h"
#include "../assets/asset.h"
#include "../engine/shaders.h"
}

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
#define SEA_TESS_PX   8.f      // target screen size of a tessellation cell
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

// world pass: the static grid, flat

// detail pass: a_pos is (u,v) in [-1,1]; the grid is warped around u_center and displaced

// --- tessellation stages. The vertex stage places lattice corners; the control stage sets the
// levels; the evaluation stage is where the old detail vertex shader's work happens.

// Levels are computed in locals and the built-ins are written once and never read back: the
// Intel driver (32.0.101.6733) crashes inside glLinkProgram when a control shader reads
// gl_TessLevelOuter/Inner after writing them (found by bisection; every other construct here
// links). Outer[1] is the edge 0-1, [2] 1-2, [3] 2-3, [0] 3-0 for the (u, v) layout the
// evaluation stage uses.



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

int   sea_set_changes;                         // sets built this run (dashboard, log)
float sea_wind_dx = 1.f, sea_wind_dz = 0.f, sea_wind_str = 1.f;   // the wind the current set was built from (dashboard)
float sea_wind_amp = 1.f;                                         // wave height factor of the current set
float sea_wind_now_str = 1.f;                                     // the wind right now at the look-at point (weather)

int sea_nyquist = 1;         // fade waves the detail grid cannot sample (MARE_SEA_NYQUIST=0 shows the aliasing it removes)
float sea_foam = 1.f;        // foam strength (dashboard / MARE_FOAM; 0 = none)
int   sea_refract = 1;       // refract the frame behind the water (MARE_SEA_REFRACT=0: the old alpha blend)
int   sea_ship_waves = 1;    // the water ships push aside (MARE_SHIP_WAVES=0 for the A/B; the foam stays)
#define SEA_MAX_SHIPS 24
float sea_wave_amp = 1.f;    // global wave height knob (dashboard / MARE_WAVES; 0 = flat water)
float sea_wave_scale = 1.f;  // set per scene; the instanced meshes read it too, so ships ride the same swell
float sea_local_depth = 0.f;
float sea_clip_z; int sea_clip_on;   // staged scenes: water only on the seaward side of this world z

// Staged scenes (staged_scene.cc) stretch the sea by DV_SHIP_SCALE, so a wave that looks right on
// the map towers over a staged ship: detailview sets this to cut the displacement back. Cell
// density - and so the silhouette of the water - is untouched.
float sea_local_amp = 1.f;

int sea_tess_now;    // the tessellated path is in use this frame (dashboard)

namespace mare::engine::render {

class Sea {
public:
    void  init();
    void  update_waves(float day, float dt);
    float height_at(float x, float z);
    void  bind_waves(const rhi_pipeline_t *p);
    void  set_ships(const float *xzhs, const float *len, int n);
    float zoom_amp();
    float amp_now();
    void  draw();
    void  draw_local(float depth, float scale, float radius);

private:
    typedef struct sea_wave_t { float dir[2], len, steep; } sea_wave_t;
    typedef struct sea_set_t { sea_wave_t w[SEA_WAVES]; float phase[SEA_WAVES]; float wdx, wdz, str, amp; int valid; } sea_set_t;

    rhi_pipeline_t sea_prg, sea_detail_prg, sea_tess_prg;
    rhi_geometry_t sea_geo, sea_detail_geo, sea_patch_geo, sea_inner_geo;
    int sea_tex_normal = -1, sea_tex_albedo = -1, sea_tex_disp = -1;   // textures.c handles
    float sea_shallow_now = 1.1f;   // depth the swell needs for full height, follows the zoom (sea_update_waves)
    sea_set_t sea_set[2];                   // [0] current, [1] the one fading in
    float sea_blend = 0;                        // 0 = set 0 alone .. 1 = set 1 alone
    float sea_wave_jit[SEA_WAVES][4];       // per-wave randomness: angle, length, steepness, phase offset
    int   sea_wave_seeded = 0;
    float sea_drift1[2], sea_drift2[2];     // accumulated texture offsets of the two ripple layers
    float sea_ships[SEA_MAX_SHIPS * 4], sea_ship_len[SEA_MAX_SHIPS]; int sea_nships = 0;

    static float sea_hash01(unsigned h);
    static float sea_amp_for(float str);
    void sea_set_build(sea_set_t *set, float wdx, float wdz, float str);
    void sea_bind_common(const rhi_pipeline_t *p, float wave_scale, float local_depth);
    void sea_draw_lattice(float cx, float cz, float radius, int n, float scale, float local_depth, float amp, int inner, float outer_patch, float hole_r);
    void sea_draw_patches(float cx, float cz, float radius, float scale, float local_depth, float amp);
};

Sea &sea() { static Sea s; return s; }

void Sea::init() {
    static const char *sea_uniforms[] = { GLSL_WAVE_UNIFORMS, "u_drift1", "u_drift2", "u_origin", "u_patch", "u_tess", "u_disp", "u_has_disp", "u_foam", "u_calm", "u_ships", "u_shiplen", "u_nships", "u_shipwave", "u_shallow", "u_scene", "u_scene_depth", "u_scene_size", "u_refract", "u_cam_right", "u_cam_up", "u_vp", "u_hm", "u_waves", "u_albedo", "u_has_albedo", "u_geo", "u_k", "u_time",
                                          "u_campos", "u_mapness", "u_sky", "u_daylight", "u_center", "u_detail",
                                          "u_wave_amp", "u_wave_scale", "u_local_depth", "u_hole", "u_clip", "u_sea_debug",
                                          "u_det", "u_det_far", "u_det_rect", "u_det_far_rect", "u_det_on", "u_det_texel", "u_mapness_vs",
                                          "u_shore", "u_shore_far", "u_detail_far", "u_terr", "u_terr_texel", GLSL_MAPMODE_UNIFORMS, GLSL_LIGHT_UNIFORMS, 0 };
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    // (C compound literals -> named descriptors: C++ cannot take &T{...}; the tess one is also
    // reordered to the struct's declaration order: vs, fs, tcs, tes, patch_verts, layout, ...)
    const rhi_pipeline_desc_t sea_desc{ .vs = shader_source("sea/sea.vert"),        .fs = shader_source("sea/sea.frag"), .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA };
    sea_prg        = rhi_pipeline_make(&sea_desc);
    const rhi_pipeline_desc_t sea_detail_desc{ .vs = shader_source("sea/sea_detail.vert"), .fs = shader_source("sea/sea.frag"), .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA };
    sea_detail_prg = rhi_pipeline_make(&sea_detail_desc);
    sea_tess_on = rhi_tessellation_supported();
    { const char *e = getenv("MARE_SEA_TESS"); if (e && !atoi(e)) sea_tess_on = 0; }
    if (sea_tess_on) {
        const rhi_pipeline_desc_t sea_tess_desc{ .vs = shader_source("sea/sea_tess.vert"), .fs = shader_source("sea/sea.frag"), .tcs = shader_source("sea/sea.tesc"), .tes = shader_source("sea/sea.tese"), .patch_verts = 4,
                                                 .layout = &layout, .uniforms = sea_uniforms, .depth_test = 1, .depth_write = 1, .blend = RHI_BLEND_ALPHA };
        sea_tess_prg = rhi_pipeline_make(&sea_tess_desc);
        // patch lattices: (i, j) indices, four corners per patch counter-clockwise seen from above
        for (int which = 0; which < 2; ++which) {
            int n = which ? SEA_INNER_N : SEA_PATCH_N, vn = n + 1;
            float *v = (float *)malloc((size_t)vn*vn*2*sizeof(float));
            for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[(y*vn+x)*2] = (float)x; v[(y*vn+x)*2+1] = (float)y; }
            unsigned *idx = (unsigned *)malloc((size_t)n*n*4*sizeof(unsigned)); int m = 0;
            for (int y = 0; y < n; ++y) for (int x = 0; x < n; ++x) { unsigned a = y*vn+x; idx[m++] = a; idx[m++] = a+1; idx[m++] = a+vn+1; idx[m++] = a+vn; }
            *(which ? &sea_inner_geo : &sea_patch_geo) = rhi_geometry_make(&layout, v, vn*vn, idx, m);
            free(v); free(idx);
        }
        LOG_INFO("gpu", "sea: tessellated patches (%dx%d, radius %.0f, cells ~%.0f px, up to %d per edge), displacement map on", SEA_PATCH_N, SEA_PATCH_N, SEA_PATCH_R, SEA_TESS_PX, sea_tess_level_max);
    } else LOG_INFO("gpu", "sea: warped grid (no tessellation on this context)");
    {   // world grid: static, covers the whole map
        int nx = 88, ny = 65;
        float *v = (float *)malloc((nx+1)*(ny+1)*2*sizeof(float));
        float x0 = lon2x(LON0), x1 = lon2x(LON1), z0 = lat2z(LAT1), z1 = lat2z(LAT0);
        for (int y = 0; y <= ny; ++y) for (int x = 0; x <= nx; ++x) { v[(y*(nx+1)+x)*2] = lerpf(x0,x1,x/(float)nx); v[(y*(nx+1)+x)*2+1] = lerpf(z0,z1,y/(float)ny); }
        unsigned *idx = (unsigned *)malloc(nx*ny*6*sizeof(unsigned)); int m = 0;
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) { unsigned a = y*(nx+1)+x, b = a+1, c = a+nx+1, d = c+1; idx[m++]=a; idx[m++]=c; idx[m++]=b; idx[m++]=b; idx[m++]=c; idx[m++]=d; }
        sea_geo = rhi_geometry_make(&layout, v, (nx+1)*(ny+1), idx, m);
        free(v); free(idx);
    }
    {   // detail grid: parameter space [-1,1]^2, warped and placed by the vertex shader
        int n = SEA_DETAIL_N, vn = n + 1;
        float *v = (float *)malloc((size_t)vn*vn*2*sizeof(float));
        for (int y = 0; y < vn; ++y) for (int x = 0; x < vn; ++x) { v[(y*vn+x)*2] = x * 2.f / n - 1.f; v[(y*vn+x)*2+1] = y * 2.f / n - 1.f; }
        unsigned *idx = (unsigned *)malloc((size_t)n*n*6*sizeof(unsigned)); int m = 0;
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

float Sea::sea_hash01(unsigned h) {
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (h & 0xFFFFFF) / (float)0x1000000;
}

float Sea::sea_amp_for(float str) {
    float s = clampf(str, 0.f, 1.6f);
    // Height: the doldrums (strength ~0.35) leave a nearly flat sea, a strong westerly a heavy one.
    return clampf(0.10f + 0.95f * (s - 0.18f), 0.06f, 1.55f);
}

void Sea::sea_set_build(sea_set_t *set, float wdx, float wdz, float str) {
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
void Sea::update_waves(float day, float dt) {
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
    // The zoomed-out sea runs only in deep water: the gate rises with the camera, so at the
    // strategic view the swell needs ~3.6 wave-units of depth for full height and the coastal
    // shelf (and every new island's shallows) stays visibly calmer than the open sea.
    sea_shallow_now = 1.1f + 2.5f * clampf((cam.dist - 90.f) / 310.f, 0.f, 1.f);
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
float Sea::height_at(float x, float z) {
    float amp = sea_amp_now(), h = 0.f;
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
void Sea::bind_waves(const rhi_pipeline_t *p) {
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

// Ships the water should foam and heap around this frame: x, z, heading, speed in *hull lengths
// per second* (so the map's fleets and the staged battle's ships read alike) and hull length.
// scene.c gives the fleets near the look-at point, staged_scene.cc its staged ships.
void Sea::set_ships(const float *xzhs, const float *len, int n) {
    sea_nships = n > SEA_MAX_SHIPS ? SEA_MAX_SHIPS : n;
    if (sea_nships > 0) { memcpy(sea_ships, xzhs, sea_nships * 4 * sizeof(float)); memcpy(sea_ship_len, len, sea_nships * sizeof(float)); }
}

// uniforms every sea draw shares
void Sea::sea_bind_common(const rhi_pipeline_t *p, float wave_scale, float local_depth) {
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
    rhi_set_f1(rhi_uniform(p,"u_shipwave"), sea_ship_waves ? 1.f : 0.f);
    rhi_set_f1(rhi_uniform(p,"u_shallow"), sea_shallow_now);
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
    // how far the ripple normals, the glitter and the foam keep their detail: with the camera
    // it looks from, so the sun still lights the sea at the regional zoom (0.22)
    rhi_set_f1(rhi_uniform(p,"u_detail_far"), maxf(400.f * wave_scale, cam.dist * 6.f + 300.f));
    rhi_set_f1(rhi_uniform(p,"u_local_depth"), local_depth);
    rhi_set_f2(rhi_uniform(p,"u_clip"), sea_clip_z, sea_clip_on ? 1.f : 0.f);
    {   // the streamed detail rings (terrain_rings.cc), bound exactly as the terrain binds them,
        // so the shoreline the water stops at is the shoreline the ground draws (0.19)
        const rhi_texture_t *dn = local_depth > 0.f ? 0 : streamview_detail_tex(0), *df = local_depth > 0.f ? 0 : streamview_detail_tex(1);
        float rn[4] = { 0, 0, 1, 1 }, rf[4] = { 0, 0, 1, 1 };
        if (dn) streamview_detail_rect(0, rn);
        if (df) streamview_detail_rect(1, rf);
        const rhi_texture_t *hm = terrain_heightmap_texture();
        rhi_set_i1(rhi_uniform(p,"u_det"), 8);     rhi_texture_bind(8, dn ? dn : hm);
        rhi_set_i1(rhi_uniform(p,"u_det_far"), 9); rhi_texture_bind(9, df ? df : hm);
        rhi_set_f4(rhi_uniform(p,"u_det_rect"), rn[0], rn[1], rn[2], rn[3]);
        rhi_set_f4(rhi_uniform(p,"u_det_far_rect"), rf[0], rf[1], rf[2], rf[3]);
        rhi_set_f2(rhi_uniform(p,"u_det_on"), dn ? 1.f : 0.f, df ? 1.f : 0.f);
        rhi_set_f2(rhi_uniform(p,"u_det_texel"), streamview_detail_texel(0), streamview_detail_texel(1));
        rhi_set_f1(rhi_uniform(p,"u_mapness_vs"), RF.mapness);
        // and the rings' shore masks (cliff, beach, marsh, exposure): where the waves break
        const rhi_texture_t *sn = local_depth > 0.f ? 0 : streamview_shore_tex(0), *sf = local_depth > 0.f ? 0 : streamview_shore_tex(1);
        rhi_set_i1(rhi_uniform(p,"u_shore"), 10);     rhi_texture_bind(10, sn ? sn : hm);
        rhi_set_i1(rhi_uniform(p,"u_shore_far"), 11); rhi_texture_bind(11, sf ? sf : hm);
    }
    // the map modes (0.22): the sea is coloured by depth or dimmed under a thematic map
    map_mode_bind(p);
    rhi_set_i1(rhi_uniform(p,"u_terr"), 17); rhi_texture_bind(17, terrain_territory_texture());
    rhi_set_f2(rhi_uniform(p,"u_terr_texel"), 1.f/TERR_W, 1.f/TERR_H);
    { static int dbg = -1; if (dbg < 0) { const char *e = getenv("MARE_SEA_DEBUG"); dbg = e ? atoi(e) : 0; }
      rhi_set_f1(rhi_uniform(p,"u_sea_debug"), dbg ? 1.f : 0.f); }
    light_bind(p);
}

// Wave height follows the zoom (DESIGN_STREAMING.md task 6): full displacement only down at
// the water, flattening as the camera pulls out - the tessellated sea keeps its cell density
// but loses height, which is what reads as distance instead of chop.
//
// Past the regional view a swell the size of a county is nonsense: the strategic and
// continental tiers want a sea that reads as texture, so the curve keeps falling to a near-flat
// floor rather than levelling off where it used to.
float Sea::zoom_amp() {
    float d = cam.dist;
    if (d <= 40.f)  return 1.f;                                           // city / battle: full height
    if (d <= 120.f) return lerpf(1.f, 0.42f, (d - 40.f) / 80.f);          // regional: falling away fast
    if (d <= 300.f) return lerpf(0.42f, 0.18f, (d - 120.f) / 180.f);
    // Strategic: a swell is only readable in the zoomed-in strategic band, and small - a wave
    // the eye can measure against a hull must never read tens of metres tall. Past that band
    // the displacement goes to zero and the ripple textures carry the sea on their own.
    if (d <= 500.f) return lerpf(0.18f, 0.07f, (d - 300.f) / 200.f);
    return clampf(lerpf(0.07f, 0.f, (d - 500.f) / 400.f), 0.f, 0.07f);
}

// how tall the waves may be right now: they flatten as the view turns into a map and with the
// zoom, and the detail grid only exists while the camera is close enough for it to matter.
// Everything that has to agree about the water surface goes through this: the sea passes, the
// instanced meshes (so ships ride what they float on) and the camera's wave-crest floor.
float Sea::amp_now() { return sea_wave_amp * sea_wind_amp * (1.f - RF.mapness) * zoom_amp() * sea_local_amp; }

// The tessellated detail region: patches on a world lattice around the look-at point.
// `radius` and `scale` as for the warped grid. Shared by the map and the staged scenes.
void Sea::sea_draw_lattice(float cx, float cz, float radius, int n, float scale, float local_depth, float amp, int inner, float outer_patch, float hole_r) {
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
void Sea::sea_draw_patches(float cx, float cz, float radius, float scale, float local_depth, float amp) {
    float outer_patch = 2.f * radius / SEA_PATCH_N, inner_r = SEA_INNER_R * scale;
    int inner = sea_inner_on && cam.dist < 60.f * scale;      // only when the camera can be close to the water
    sea_draw_lattice(cx, cz, radius, SEA_PATCH_N, scale, local_depth, amp, 0, outer_patch, inner ? inner_r * 0.985f : 0.f);
    if (inner) sea_draw_lattice(cx, cz, inner_r, SEA_INNER_N, scale, local_depth, amp, 1, outer_patch, 0.f);
}

void Sea::draw() {
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

// Staged scenes (staged_scene.cc): the detail grid alone, centred on the camera, at a constant
// depth and with no land to discard. `scale` stretches the wave field to the scene's units.
// `clip_z` (when `clip_on`) is the shoreline: a staged scene has no heightmap, so the water's
// landward limit is given as a plane rather than discovered from the terrain.
void Sea::draw_local(float depth, float scale, float radius) {
    sea_tess_now = sea_tess_on && cam.dist < SEA_TESS_DIST * scale;
    if (sea_tess_now) { sea_draw_patches(RF.sea_center.x, RF.sea_center.z, radius, scale, depth, sea_amp_now()); return; }
    rhi_pipeline_bind(&sea_detail_prg);
    sea_bind_common(&sea_detail_prg, scale, depth);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_center"), RF.sea_center.x, 0, RF.sea_center.z);
    rhi_set_f3(rhi_uniform(&sea_detail_prg,"u_detail"), radius, SEA_WARP, sea_nyquist ? 2.f / SEA_DETAIL_N : 0.f);
    rhi_set_f1(rhi_uniform(&sea_detail_prg,"u_wave_amp"), sea_amp_now());
    rhi_set_f4(rhi_uniform(&sea_detail_prg,"u_hole"), 0, 0, 0, 0);
    rhi_draw(&sea_detail_geo, 0);
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
void  sea_init(void) { mare::engine::render::sea().init(); }
void  sea_update_waves(float day, float dt) { mare::engine::render::sea().update_waves(day, dt); }
float sea_height_at(float x, float z) { return mare::engine::render::sea().height_at(x, z); }
void  sea_bind_waves(const rhi_pipeline_t *p) { mare::engine::render::sea().bind_waves(p); }
void  sea_set_ships(const float *xzhs, const float *len, int n) { mare::engine::render::sea().set_ships(xzhs, len, n); }
float sea_zoom_amp(void) { return mare::engine::render::sea().zoom_amp(); }
float sea_amp_now(void) { return mare::engine::render::sea().amp_now(); }
void  sea_draw(void) { mare::engine::render::sea().draw(); }
void  sea_draw_local(float depth, float scale, float radius) { mare::engine::render::sea().draw_local(depth, scale, radius); }
}
