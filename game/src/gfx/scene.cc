// Scene assembly (see scene.h): LOD selection for ships and ports, procedural towns, icons,
// debug-draw overlays (paths, routes, lanes, storms, wind).
// mare::game::view::SceneBuilder: the per-frame scene state (instance lists, town caches, fleet
// animation, authored-asset residency, icon clustering) and its behaviour. The C ABI (scene.h)
// is a set of shims over the singleton at the bottom; the shared globals stay plain C globals.
#include "../platform/v3cpp.hh"
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "scene.h"
#include "render.h"
#include "camera.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/diag.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/wind.h"
#include "../sim/economy.h"
#include "../sim/fleet.h"
#include "../sim/events.h"
#include "../sim/settlement.h"
#include "../world/regions.h"
#include "../assets/asset.h"
#include "detailview.h"
#include "lod.h"
#include "occlusion.h"
#include "occquery.h"
#include "shipyard.h"
}

static_assert(MB_FLOATS == 12, "asset.c emits 12-float builder vertices (ASSET_VERT_FLOATS)");

// Shared globals (declared extern "C" in scene.h): plain globals, defined here outside the namespace.
scene_hints_t SH;
int scene_culled_ports, scene_culled_ships;   // skipped by frustum culling this frame (dashboard)
int scene_occluded_assets;                     // authored instances the GPU queries hid this frame (left out of the main pass)
int scene_occluded_ports, scene_occluded_ships; // skipped because terrain hides them (occlusion.c)
int scene_shadow_ports, scene_shadow_fleets;   // collected this frame for their shadows alone (dashboard)
int shadow_caster_lod = 1;               // 0 = cast with the drawn LOD (dashboard toggle / MARE_SHADOW_LOD=0)
int scene_shadow_tris;                   // triangles the last shadow cascade submitted (dashboard)
int scene_asset_instances;               // dashboard: authored-asset instances drawn this frame
int scene_assets_evicted;                // assets dropped from the GPU this session (dashboard)
int scene_asset_lod_fallbacks;           // instances drawn at a coarser LOD than picked while the finer one streams in
int scene_icons_clustered;        // markers merged away this frame (dashboard)

namespace mare::game::view {

// Levels of detail are chosen in screen space (lod.h): ships have [full mesh, simplified, icon],
// towns [town, icon], authored assets their own LODs plus the town's icon hand-over. Each
// instance keeps hysteresis and cross-fade state under a stable key.
static const float ship_thr[2] = { LOD_PX_SHIP_SIMPLE, LOD_PX_SHIP_ICON };
static const float town_thr[1] = { LOD_PX_TOWN_ICON };

typedef struct fleet_anim_t { int id; float x, z, heading, speed; } fleet_anim_t;
enum { TM_HOUSE, TM_ROOF, TM_TOWER, TM_WALL, TM_PIER, TM_FLAG, TM_ASSET };
typedef struct town_item_t { unsigned char mesh; short asset; inst_t in; } town_item_t;   // asset: registry index for TM_ASSET
typedef struct town_cache_t { unsigned sig; int n; town_item_t *items; } town_cache_t;
typedef struct footprint_t { float x, z, r; } footprint_t;
typedef struct icon_group_t { int cell_x, cell_y, best, count; float prio; } icon_group_t;

class SceneBuilder {
public:
    void   set_tick(float period_s, float days, float left_s);
    float  smoke_window(void);
    void   reset_anim(void);
    void   reset_caches(void);
    int    occlusion_active(void);
    void   fleet_pos(const fleet_t *f, float *x, float *z, float *heading);
    void   build_ships(void);
    int    submit_ships(void);
    int    submit_smoke(void);
    mesh_t *asset_gpu_mesh(asset_t *a, int lod);
    void   asset_upload(asset_t *a, int lod);
    void   asset_instance(asset_t *a, inst_t in, unsigned key, float camdist);
    size_t asset_gpu_budget(void);
    float  asset_idle_s(int ai);
    int    asset_lod_count(int asset_index, int lod);
    void   assets_begin(void);
    void   assets_end(void);
    int    assets_submit_shadow(void);
    int    assets_submit(void);
    void   build_ports(void);
    int    submit_ports(void);
    int    submit_shadow(void);
    void   push_icons(void);
    void   draw_overlays(void);

private:
    int    occ_hidden_sphere(float x, float z, float r, float top);
    static float cam_distance(float x, float z);
    static void  instlist_append(instlist_t *dst, const instlist_t *src);
    void   fleet_anim_pos(int i, const fleet_t *f, float dt, float *x, float *z, float *heading, float *speed);
    static void ship_station(const fleet_t *f, int s, float x, float z, float heading,
                             float hx, float hz, float *ox, float *oz, float *oh);
    int    submit_ship_meshes(int shadow);
    void   asset_push(asset_t *a, inst_t in, unsigned key, float camdist, float town_dither);
    void   scene_asset_evict(int ai);
    void   scene_assets_residency(void);
    void   tc_push(int mesh, inst_t in);
    void   tc_push_asset(asset_t *a, inst_t in);
    static unsigned town_signature(const port_t *p);
    static int  fp_free(const footprint_t *fp, int n, float x, float z, float r);
    static void ground_range(float x, float z, float h, float *lo, float *hi);
    static asset_t *town_house_asset(float ring, int size, unsigned *seed);
    void   town_build(const port_t *p, int index);
    void   town_instances(const port_t *p, int index, float dither);
    int    submit_town_meshes(int shadow);
    void   icon(float x, float z, float heading, float size, int shape, float alpha, float outline, unsigned col);
    icon_group_t *icon_cluster(float sx, float sy, int index, float prio);

    // ---- state (file-scope statics of the C version)
    float port_icon_w[MAX_PORTS], fleet_icon_w[MAX_FLEETS];   // icon alpha per port / fleet slot, from this frame's picks
    float icon_w_time = -1;                                    // RF.time the weights were computed for (stale -> icons draw fully)
    int p_occ = -1;
    float frustum[6][4];
    instlist_t ship_inst[SHIP_COUNT][2];
    // Shadow casters beyond what the main pass draws (DESIGN_VISUAL.md step 10). A town or a fleet
    // that is far enough to be an icon, behind the camera, or hidden by a building still throws a
    // shadow onto the terrain; it was simply never submitted. Those instances are collected into
    // their own lists, appended after the drawn ones before the single upload, and the main pass
    // draws the prefix while the shadow pass draws all of it - the same partition the occlusion
    // queries use, so there is still one instance buffer per mesh and no mid-frame re-upload.
    instlist_t ship_cast[SHIP_COUNT];
    int ship_vis[SHIP_COUNT][2];
    int scene_caster_only;            // town_instances/asset_push are collecting casters, not draws
    instlist_t puffs;                 // gunsmoke from recent broadsides
    fleet_anim_t fleet_anim[MAX_FLEETS];
    float anim_prev_time;
    float tick_period = 0.3f, tick_days = SIM_STEP, tick_left = 0.f;
    mesh_t    asset_mesh[ASSET_MAX][ASSET_MAX_LODS];
    instlist_t asset_inst[ASSET_MAX][ASSET_MAX_LODS];
    // Shadow casters (DESIGN_VISUAL.md step 3). A shadow map texel covers a good fraction of a
    // house, so casting the 80k-triangle LOD 0 three times (once per cascade) buys nothing a 2k
    // silhouette does not. Each asset therefore gets a second geometry built from its coarsest
    // resident LOD, with its own instance buffer holding *every* instance of that asset this frame
    // (whatever LOD the main pass picked for it, and including the ones the occlusion queries hid -
    // a hidden house still casts a visible shadow). Separate buffers because the shadow pass runs
    // before the main pass: re-uploading the shared ones between the two passes is what cost 400 ms
    // a frame on the Intel driver when the occlusion work first tried it.
    mesh_t    asset_shadow_mesh[ASSET_MAX];
    int       asset_shadow_lod[ASSET_MAX];
    instlist_t asset_shadow_inst[ASSET_MAX];
    int asset_used[ASSET_MAX];        // has an instance list this frame
    float asset_last_use[ASSET_MAX];  // RF.time an instance of this asset was last submitted
    int asset_vis[ASSET_MAX][ASSET_MAX_LODS];   // per list: instances the main pass draws (the visible ones, partitioned to the front)
    instlist_t houses, roofs, towers, walls, piers, flagpoles;
    // Shadow casters beyond what the main pass draws (DESIGN_VISUAL.md step 10). A town that is far
    // enough to be an icon, behind the camera, or hidden by another building still throws a shadow
    // onto the terrain; it was simply never submitted. Those pieces are collected into their own
    // lists, appended after the drawn ones before the single upload, and the main pass draws the
    // prefix while the shadow pass draws all of it - the same partition the occlusion queries use,
    // so there is still one instance buffer per mesh and no mid-frame re-upload.
    instlist_t sh_houses, sh_roofs, sh_towers, sh_walls, sh_piers, sh_flagpoles;
    int town_vis[6];
    town_cache_t town_cache[MAX_PORTS];
    town_cache_t *tc_cur;
    icon_group_t icon_groups[MAX_PORTS + 64];
    int n_icon_groups;
};

SceneBuilder &scene_builder() { static SceneBuilder s; return s; }

// Occlusion only means something in the 3D view: the map is flat and the staged scenes have no
// heightmap. The eye is the camera; the tallest thing that can hide a mast is a mast-high town.
int SceneBuilder::occlusion_active(void) { return occlusion_enabled && cam.mapness < 0.5f && detailview_mode() == DV_OFF; }
#define SHIP_TOP  4.5f    // masts of the largest hull, world units above the water
#define TOWN_TOP  6.0f    // church tower / fort keep above the town's ground
int SceneBuilder::occ_hidden_sphere(float x, float z, float r, float top) {
    if (p_occ < 0) p_occ = prof_id("gfx.occlusion");
    prof_begin(p_occ); int hid = occ_sphere_hidden(world_height, x, z, r, top); prof_end(p_occ);
    return hid;
}
// Margin so that objects just outside the view still cast their shadows into it: the sun sits
// high, so a shadow reaches at most ~0.8x the caster's height, plus the object's own radius.
#define CULL_MARGIN 8.f

float SceneBuilder::cam_distance(float x, float z) { float d = dist2d(x, z, cam.pos.x, cam.pos.z); return sqrtf(d*d + cam.pos.y*cam.pos.y); }

void SceneBuilder::fleet_pos(const fleet_t *f, float *x, float *z, float *heading) {
    *x = f->x; *z = f->z; *heading = f->heading;
    if (f->at_port < 0) return;
    int k = 0; for (int i = 0; i < fleet_index(f); ++i) if (G.fleets[i].alive && G.fleets[i].at_port == f->at_port) k++;
    const port_t *p = &G.ports[f->at_port];
    float ang = atan2f(p->z - p->lz, p->x - p->lx);     // land -> sea
    float r = 1.2f + 0.9f * (k / 3), a = ang + ((k % 3) - 1) * 0.55f;
    *x = p->x + cosf(a) * r; *z = p->z + sinf(a) * r; *heading = -ang + 1.5708f;   // moored broadside to the shore
}

// ---------------------------------------------------------------------------
// ships
//
// The simulation moves fleets in fixed 0.1-day steps, which at the default speed is one step
// every ~0.3 s: rendering the raw simulation position makes ships visibly jump about two thirds
// of their own length at a time. Each fleet therefore carries a render state that chases the
// simulated position exponentially, which turns those steps into continuous motion (the lag is
// well under one step). Large jumps - new game, load, teleport, slot reuse - snap instead.

void SceneBuilder::instlist_append(instlist_t *dst, const instlist_t *src) { for (int i = 0; i < src->n; ++i) *instlist_push(dst) = src->v[i]; }


#define ANIM_RATE  7.0f     // 1/s: time constant ~0.14 s, shorter than one simulation step
#define ANIM_SNAP  9.0f     // world units: beyond this the fleet did not sail there

// Tick window (scene.h): with SIM_TICK_SECONDS at 15 the simulation moves a fleet several days
// at once, so the draw position is carried to the new one over the rest of the interval instead
// of chasing it with a fixed rate. The snap threshold grows with the days a tick covered, since
// a fleet legitimately sails further in five days than in a tenth of one.
void SceneBuilder::set_tick(float period_s, float days, float left_s) { tick_period = period_s; tick_days = days; tick_left = left_s; }
float SceneBuilder::smoke_window(void) { return maxf(GUNSMOKE_DAYS, tick_days); }

void SceneBuilder::reset_anim(void) { memset(fleet_anim, 0, sizeof fleet_anim); }

// smoothed draw position for fleet slot `i`; also reports how fast it is actually moving
void SceneBuilder::fleet_anim_pos(int i, const fleet_t *f, float dt, float *x, float *z, float *heading, float *speed) {
    float tx, tz, th; fleet_pos(f, &tx, &tz, &th);
    fleet_anim_t *a = &fleet_anim[i];
    if (a->id != f->id || dist2d(a->x, a->z, tx, tz) > ANIM_SNAP + 14.f * tick_days) {
        *a = fleet_anim_t{ f->id, tx, tz, th, 0 };
    } else {
        // arrive exactly when the next tick lands; fall back to the old rate for short ticks
        float k = tick_period > 0.6f ? clampf(dt / maxf(tick_left, dt), 0.f, 1.f) : 1.f - expf(-dt * ANIM_RATE);
        float nx = a->x + (tx - a->x) * k, nz = a->z + (tz - a->z) * k;
        a->speed = dt > 1e-5f ? lerpf(a->speed, dist2d(a->x, a->z, nx, nz) / dt, 0.2f) : a->speed;
        a->x = nx; a->z = nz;
        a->heading += wrap_angle(th - a->heading) * k;
    }
    *x = a->x; *z = a->z; *heading = a->heading; *speed = a->speed;
}

// Station keeping (every ship in a fleet sails for itself).
//
// A fleet is one entity in the simulation, but drawing its ships as a rigid line of boxes
// welded to a single position is what made a squadron read as one object sliding across the
// water. Each ship instead keeps a *station* - its place in the line - and wanders around it
// on its own slow cycle, with its own heading deviation, so the formation breathes: ships
// close up, fall off, and come back without ever leaving the fleet behind. The cycle is a
// hash of the fleet id and the ship's slot, so it is stable from frame to frame, continuous,
// costs no storage, and two ships never move alike.
//
// Moored ships keep their berth: scene_fleet_pos has already fanned them around the harbour.
#define STATION_GAP    1.42f    // along-track spacing of the line (1.05 * 1.35, as before)
#define STATION_WANDER 0.40f    // how far a ship strays from its station, world units
#define STATION_YAW    0.14f    // and how far its heading strays, radians

void SceneBuilder::ship_station(const fleet_t *f, int s, float x, float z, float heading,
                         float hx, float hz, float *ox, float *oz, float *oh) {
    float along = -(s * STATION_GAP), side = ((s % 2) ? 0.45f : -0.45f) * (s > 0) * 1.35f;
    float yaw = 0.f;
    if (f->at_port < 0) {
        unsigned h = lod_key(LODK_FLEET, f->id, s + 1);
        float p1 = (h & 1023u) / 1023.f * 6.2832f, p2 = ((h >> 10) & 1023u) / 1023.f * 6.2832f;
        float rate = 0.7f + 0.6f * (((h >> 20) & 255u) / 255.f);      // each ship on its own clock
        float t = RF.time * rate;
        along += STATION_WANDER * sinf(t * 0.21f + p1);
        side  += STATION_WANDER * 0.85f * sinf(t * 0.17f + p2);
        yaw    = STATION_YAW * sinf(t * 0.23f + p1 * 1.7f);
    }
    *ox = x + hx * along - hz * side;
    *oz = z + hz * along + hx * side;
    *oh = heading + yaw;
}

#define BATTLE_DETAIL_MAX 50     // full-detail ships a battle may add beyond the normal LOD rules

void SceneBuilder::build_ships(void) {
    cam_frustum(frustum);
    scene_culled_ships = scene_occluded_ships = 0;
    shipyard_oars_drawn = shipyard_ships_drawn = shipyard_fallbacks = 0;
    shipyard_frame_begin();
    int occ = occlusion_active();
    int battle_lod0_budget = BATTLE_DETAIL_MAX;
    float dt = clampf(RF.time - anim_prev_time, 0, 0.25f); anim_prev_time = RF.time;
    for (int t = 0; t < SHIP_COUNT; ++t) { for (int l = 0; l < 2; ++l) instlist_clear(&ship_inst[t][l]); instlist_clear(&ship_cast[t]); }
    instlist_clear(&puffs);
    scene_shadow_fleets = 0;
    // ships the sea foams around (sea_set_ships): the nearest to the look-at point, up to 24
    float foam_v[24 * 4], foam_len[24], foam_d[24]; int nfoam = 0;
    for (int i = 0; i < G.nfleets; ++i) {
        const fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        float x, z, heading, vel; fleet_anim_pos(i, f, dt, &x, &z, &heading, &vel);
        float d = cam_distance(x, z), r = 1.4f * 1.35f + f->nships * 0.2f;   // same footprint the picker uses
        float px = cam.mapness > 0.6f ? 0.f : lod_px(r, d);                   // map view: everything is an icon
        lod_pick_t pk = lod_select(lod_key(LODK_FLEET, f->id, 0), ship_thr, 3, px, 1);
        fleet_icon_w[i] = lod_weight(&pk, 2);
        int draws = lod_draws(&pk, 0) || lod_draws(&pk, 1);                 // else: icon only
        if (draws && !cam_sphere_visible(frustum, x, 0, z, f->nships * 1.5f + CULL_MARGIN)) { scene_culled_ships++; draws = 0; }
        if (draws && occ && occ_hidden_sphere(x, z, f->nships * 1.5f + 1.f, SHIP_TOP)) { scene_occluded_ships++; draws = 0; }
        if (!draws) {
            // not drawn, but inside the shadow radius: its hulls still cast, at the simplified LOD
            if (shadow_enabled && d < shadow_radius && cam.mapness < 0.5f && f->at_port < 0) {
                unsigned ccol = f->role == ROLE_PIRATE ? rgb(20,20,20) : nations_info[f->nation].color;
                float chx = cosf(heading), chz = -sinf(heading);
                int nshow = mini(f->nships, 4);
                for (int s = 0; s < nshow; ++s) {
                    float sc = ship_scale(f->ships[s].type);
                    float along = -(s * 1.05f) * 1.35f, side = ((s % 2) ? 0.45f : -0.45f) * (s > 0) * 1.35f;
                    *instlist_push(&ship_cast[f->ships[s].type]) =
                        inst_t{ x + chx*along - chz*side, 0, z + chz*along + chx*side, heading, sc, col_r(ccol), col_g(ccol), col_b(ccol), 0, 0, 1, 0, 0 };
                }
                scene_shadow_fleets++;
            }
            continue;
        }
        unsigned col = f->role == ROLE_PIRATE ? rgb(20,20,20) : nations_info[f->nation].color;
        int sel = f->id == SH.sel_fleet_id, hov = f->id == SH.hover_fleet_id;
        // Battle view: a fleet that fired recently is being watched - it keeps the full mesh and
        // its whole line of ships at the simplified level too, so a spectated battle reads ship
        // for ship instead of collapsing to 4 simplified hulls. A frame budget caps the extra
        // detail: a melee stops promoting at ~BATTLE_DETAIL_MAX ships and falls back to LODs.
        int in_battle = f->at_port < 0 && f->last_fire > G.day - maxf(0.3f, smoke_window());
        float hx = cosf(heading), hz = -sinf(heading);
        // how hard the ship works: moored ships barely move, ships under way roll with their
        // speed, and a storm throws everything around
        const event_t *storm = storm_at(f->x, f->z);
        float rock = (f->at_port >= 0 ? 0.25f : 0.55f + 0.45f * clampf(vel / 9.f, 0, 1))
                   * (storm ? 1.f + 1.6f * storm->strength : 1.f);
        // Authored hulls first (shipyard.h): a class that has finished loading draws the whole
        // line through the asset path - its own five LODs replace the full/simplified pair, so
        // those ships are left out of the procedural passes below. The fleet's mesh-to-icon
        // dither still applies, so the hand-over to the marker is the same dissolve as before.
        int authored[MAX_SHIPS]; memset(authored, 0, sizeof authored);
        float sx_[MAX_SHIPS], sz_[MAX_SHIPS], sh_[MAX_SHIPS];
        for (int s = 0; s < f->nships; ++s)
            ship_station(f, s, x, z, heading, hx, hz, &sx_[s], &sz_[s], &sh_[s]);
        for (int s = 0; s < f->nships; ++s) {
            shipyard_req_t sr = { f->ships[s].type, f->nation, f->role, sx_[s], sz_[s], sh_[s], 1.f,
                                  rock, sel ? 1.f : hov ? 0.5f : 0.f, lod_dither(&pk, pk.lod),
                                  col_r(col), col_g(col), col_b(col),
                                  cam_distance(sx_[s], sz_[s]), lod_key(LODK_FLEET, f->id, s + 1) };
            authored[s] = shipyard_push(&sr);
        }
        for (int pass = 0; pass < 2; ++pass) {                                // the level shown, then the one fading in
            int level = pass == 0 ? pk.lod : pk.lod2;
            if (level < 0 || level > 1) continue;
            int lod = level, nshow = lod == 1 ? mini(f->nships, 4) : f->nships;
            if (in_battle && lod == 1 && battle_lod0_budget >= f->nships) { lod = 0; nshow = f->nships; battle_lod0_budget -= f->nships; }
            float dith = lod_dither(&pk, level);
            for (int s = 0; s < nshow; ++s) {
                float sc = ship_scale(f->ships[s].type);
                float wx = sx_[s], wz = sz_[s], wh = sh_[s];
                if (!authored[s]) {
                    inst_t *in = instlist_push(&ship_inst[f->ships[s].type][lod]);
                    *in = inst_t{ wx, 0, wz, wh, sc, col_r(col), col_g(col), col_b(col), rock, sel ? 1.f : hov ? 0.5f : 0.f, 1, 0, dith };
                }
                if (pass == 0 && f->at_port < 0) {           // moored ships leave no splash
                    float dd = dist2d(wx, wz, cam.tx, cam.tz);
                    if (dd < 90.f) {
                        int k = nfoam < 24 ? nfoam++ : -1;
                        if (k < 0) { float worst = -1; for (int q = 0; q < 24; ++q) if (foam_d[q] > worst) { worst = foam_d[q]; k = q; } if (worst <= dd) k = -1; }
                        if (k >= 0) { foam_v[k*4] = wx; foam_v[k*4+1] = wz; foam_v[k*4+2] = wh; foam_v[k*4+3] = vel / sc; foam_len[k] = sc; foam_d[k] = dd; }   // speed in hull lengths/s
                    }
                }
            }
        }
        if (sel || hov) dd_circle(vec3(x, 0.1f, z), vec3(0,1,0), (sel ? 1.6f : 1.4f) * 1.35f + f->nships * 0.2f, dd_opts().with_color(rgb_linear(sel ? rgb(255, 240, 120) : rgb(220, 220, 220))));
        // Gunsmoke: a couple of puffs off the engaged broadside that swell, drift and thin out.
        // (This used to be a debug-draw wire sphere, which read as a gold scribble on the water.)
        float age = G.day - f->last_fire, smoke_win = smoke_window();
        if (age >= 0 && age < smoke_win && f->at_port < 0) {
            float t = age / smoke_win, env = sinf(3.14159f * t);
            static int logged; if (!logged) { logged = 1; LOG_DEBUG("gfx", "gunsmoke: first broadside rendered (%s)", f->name); }
            for (int k = 0; k < 2; ++k) {
                float along = (k ? -0.7f : 0.55f) * 1.35f;
                inst_t *pf = instlist_push(&puffs);
                *pf = inst_t{ x + hx*along + hz*(0.6f + 0.8f*t), 0.3f + 0.9f*t, z + hz*along - hx*(0.6f + 0.8f*t),
                                0, (0.35f + 0.8f*t) * env * (k ? 0.8f : 1.f), 0.86f, 0.85f, 0.84f, 0, 0, 0, 0 };
            }
        }
    }
    for (int t = 0; t < SHIP_COUNT; ++t) {   // drawn hulls first, then the ones that only cast
        for (int l = 0; l < 2; ++l) ship_vis[t][l] = ship_inst[t][l].n;
        instlist_append(&ship_inst[t][1], &ship_cast[t]);
    }
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) mesh_set_instances(&ship_mesh[t][l], ship_inst[t][l].v, ship_inst[t][l].n);
    shipyard_upload();   // the procedural rigging and canvas of every authored ship this frame
    mesh_set_instances(&mesh_puff, puffs.v, puffs.n);
    sea_set_ships(foam_v, foam_len, nfoam);
    icon_w_time = RF.time;
}

// submit the instances uploaded by scene_build_* with whatever pipeline is bound (main or shadow)
int SceneBuilder::submit_ship_meshes(int shadow) {
    int n = 0;
    for (int t = 0; t < SHIP_COUNT; ++t) for (int l = 0; l < 2; ++l) {
        mesh_t *m = &ship_mesh[t][l]; int all = m->ninst;
        if (!shadow && ship_vis[t][l] < all) m->ninst = ship_vis[t][l];
        mesh_draw(m); n += m->ninst;
        m->ninst = all;
    }
    return n;
}
int SceneBuilder::submit_ships(void) { return submit_ship_meshes(0) + shipyard_submit(); }

// gunsmoke, drawn after the sea with the translucent pipeline (meshes_bind_smoke)
int SceneBuilder::submit_smoke(void) { mesh_draw(&mesh_puff); return mesh_puff.ninst; }

// ---------------------------------------------------------------------------
// authored assets (asset.h): GPU meshes are created on first use from the asset's CPU LODs,
// which are then released. One instance list per asset and LOD; the LOD is picked per instance
// from the camera distance, so a town can show a detailed church up close and a 3k-triangle
// one from afar in the same frame.


// Residency (DESIGN_LOD.md step 7). The wanted set is a property of the view tier: close in,
// every authored asset belongs on the GPU and is requested ahead of being asked for; zoomed out
// past the point where buildings are drawn at all, assets that have not been touched for a
// while are evicted once the GPU budget is exceeded, coldest first. A reload goes back through
// the job system (disk -> parse -> upload) and the town draws its procedural fallback in the
// meantime, so streaming costs detail, never objects.
#define ASSET_GPU_BUDGET  (192u * 1024u * 1024u)
#define ASSET_EVICT_SECS  12.f

// GPU mesh for one asset LOD, uploaded on first use (CPU copy freed after). Shared with the
// detail view (detailview.c), which draws the same meshes in its own staged scenes.
mesh_t *SceneBuilder::asset_gpu_mesh(asset_t *a, int lod) {
    int ai = (int)(a - asset_at(0));
    asset_lod_t *L = &a->lod[lod];
    if (!L->gpu) {
        asset_mesh[ai][lod] = mesh_upload_indexed(L->verts, L->nverts, L->idx, L->nidx);
        L->gpu = 1;
        a->gpu_bytes += (size_t)L->nverts * MB_FLOATS * sizeof(float) + (size_t)L->nidx * sizeof(unsigned);
        asset_free_cpu(a, lod);
        LOG_DEBUG("gfx", "asset '%s' LOD %d uploaded (%d tris)", a->desc.name, lod, L->nidx / 3);
    }
    return &asset_mesh[ai][lod];
}

// The renderer's upload hook (asset.c calls it on the MAIN lane, one LOD per budgeted job).
// The caster geometry has to be made here, from the coarsest LOD, because scene_asset_mesh
// frees the CPU copy as soon as it has uploaded it - by the time anything draws the asset there
// is nothing left to build a second geometry from. A private geometry rather than a second
// mesh_t over the drawn one: the shadow pass runs before the main pass and they need different
// instance sets, and two mesh_t sharing a geometry would share its instance buffer.
void SceneBuilder::asset_upload(asset_t *a, int lod) {
    int ai = (int)(a - asset_at(0));
    if (lod == a->nlods - 1 && !asset_shadow_mesh[ai].geo.vao) {
        asset_lod_t *L = &a->lod[lod];
        if (L->verts && L->idx) {
            asset_shadow_mesh[ai] = mesh_upload_indexed(L->verts, L->nverts, L->idx, L->nidx);
            asset_shadow_lod[ai] = lod;
            LOG_DEBUG("gfx", "asset '%s' casts shadows from LOD %d (%d tris, drawn LOD 0 is %d)",
                      a->desc.name, lod, L->nidx / 3, a->lod[0].nidx / 3);
        }
    }
    asset_gpu_mesh(a, lod);
}

// One instance of an authored asset: its LOD is picked in screen space from the authored
// switch distances (converted to pixels for the reference view, scaled by the LOD bias), with
// the instance's own hysteresis/fade state. When the whole town is dissolving into its icon,
// that fade takes precedence over the asset's own (one dither pattern per instance).
void SceneBuilder::asset_instance(asset_t *a, inst_t in, unsigned key, float camdist) { asset_push(a, in, key, camdist, 0.f); }
void SceneBuilder::asset_push(asset_t *a, inst_t in, unsigned key, float camdist, float town_dither) {
    int ai = (int)(a - asset_at(0));
    asset_last_use[ai] = RF.time;
    // Collecting casters only (a town the main pass does not draw): the coarse caster mesh is
    // the whole job - no LOD pick, no occlusion query, nothing in the drawn lists.
    if (scene_caster_only) {
        if (shadow_caster_lod) { inst_t sh = in; sh.dither = 0; sh.fx1 = 0; *instlist_push(&asset_shadow_inst[ai]) = sh; }
        asset_used[ai] = 1;
        return;
    }
    // GPU occlusion (occquery.h): ask for this frame's answer and apply the last one. A hidden
    // instance stays in the list (the shadow pass needs it) flagged in fx1.
    if (occq_active()) { occq_request(key, a, &in); if (!occq_visible(key)) { in.fx1 = 1.f; scene_occluded_assets++; } }
    // one caster instance per instance, at the coarse LOD and without the cross-fade dither:
    // the caster is a single mesh, so there is no second level to partition pixels with
    if (shadow_caster_lod) { inst_t sh = in; sh.dither = 0; sh.fx1 = 0; *instlist_push(&asset_shadow_inst[ai]) = sh; }
    float r = a->radius * (in.scale > 0 ? in.scale : 1.f), thr[ASSET_MAX_LODS];
    for (int l = 1; l < a->nlods; ++l) thr[l - 1] = a->lod[l].px > 0 ? a->lod[l].px / asset_lod_bias : lod_px_ref(r, a->lod[l].dist * asset_lod_bias);
    lod_pick_t pk = lod_select(key, thr, a->nlods, lod_px(r, camdist), a->nlods - 1);
    // LOD 0 is the original scan - hundreds of thousands of triangles - and belongs to the
    // views you actually inspect a thing in. On the strategic map a great carrack can still
    // project large enough to ask for it, which is a lot of triangles for a ship the player is
    // looking *past*; the tier, not the pixel size, decides whether it may be reached for.
    if (a->nlods > 1 && lod_tier() > TIER_BATTLE) {
        if (pk.lod == 0)  pk.lod = 1;
        if (pk.lod2 == 0) pk.lod2 = 1;
        if (pk.lod2 == pk.lod) pk.lod2 = -1;
    }
    // Progressive residency (DESIGN_STREAMING.md task 4): draw the nearest *resident* LOD at or
    // coarser than the pick instead of forcing a mid-frame upload of the picked one. Uploads
    // arrive coarse-to-fine through the budgeted MAIN jobs (asset.c), so a city walks
    // icon -> LOD4 -> ... -> LOD0; until anything is resident the icon (or the procedural town)
    // holds the fort for a frame or two.
    int use[2] = { -1, -1 };
    for (int pass = 0; pass < 2; ++pass) {
        int level = pass == 0 ? pk.lod : pk.lod2;
        if (level < 0 || level >= a->nlods) continue;
        for (int l = level; l < a->nlods; ++l)
            if (a->lod[l].gpu && asset_mesh[ai][l].geo.vao) { use[pass] = l; break; }
        if (use[pass] != level) {
            // picked a level that is not on the GPU: ask for it (the commit leaves the finest
            // LOD unqueued on purpose) and draw the coarser resident one meanwhile
            asset_upload_lod(a, level);
            if (use[pass] >= 0) scene_asset_lod_fallbacks++;
        }
    }
    if (use[1] == use[0]) use[1] = -1;                   // both ends of a fade on the same resident mesh: draw it once, opaque
    for (int pass = 0; pass < 2; ++pass) {
        if (use[pass] < 0) continue;
        int level = pass == 0 ? pk.lod : pk.lod2;
        in.dither = town_dither != 0.f ? town_dither : (use[1] < 0 ? 0.f : lod_dither(&pk, level));
        *instlist_push(&asset_inst[ai][use[pass]]) = in;
    }
    asset_used[ai] = 1;
}

size_t SceneBuilder::asset_gpu_budget(void) { return ASSET_GPU_BUDGET; }
float  SceneBuilder::asset_idle_s(int ai) { return ai >= 0 && ai < ASSET_MAX && asset_last_use[ai] > 0 ? (float)(RF.time - asset_last_use[ai]) : -1.f; }
// dashboard accessors: instances per asset and LOD this frame
int SceneBuilder::asset_lod_count(int asset_index, int lod) {
    return asset_index >= 0 && asset_index < ASSET_MAX && lod >= 0 && lod < ASSET_MAX_LODS ? asset_inst[asset_index][lod].n : 0;
}
// Free every GPU mesh of one asset and put it back to UNLOADED, so the next request reloads it.
void SceneBuilder::scene_asset_evict(int ai) {
    asset_t *a = asset_at(ai);
    if (!a || a->state != ASSET_LOADED) return;
    for (int l = 0; l < ASSET_MAX_LODS; ++l) if (asset_mesh[ai][l].geo.vao) { rhi_geometry_free(&asset_mesh[ai][l].geo); asset_mesh[ai][l].ninst = 0; }
    if (asset_shadow_mesh[ai].geo.vao) { rhi_geometry_free(&asset_shadow_mesh[ai].geo); asset_shadow_mesh[ai].ninst = 0; }
    LOG_DEBUG("gfx", "evicting asset '%s' (%.1f MB, unused %.0f s)", a->desc.name, a->gpu_bytes / (1024.0*1024.0), RF.time - asset_last_use[ai]);
    asset_unload(a);                     // frees any CPU copy, clears the LOD table, bumps the generation
    scene_assets_evicted++;
}

// Once per frame: hold the wanted set for the current tier.
void SceneBuilder::scene_assets_residency(void) {
    int tier = lod_tier();
    if (tier <= TIER_REGIONAL) {         // close enough that buildings are drawn: everything belongs here
        for (int i = 0; i < asset_count(); ++i) { asset_t *a = asset_at(i); if (a->state == ASSET_UNLOADED) asset_request(a); }
        return;
    }
    if (asset_gpu_bytes() <= ASSET_GPU_BUDGET) return;
    int coldest = -1;
    for (int i = 0; i < asset_count(); ++i) {
        const asset_t *a = asset_at(i);
        if (a->state != ASSET_LOADED || !a->gpu_bytes) continue;
        if (RF.time - asset_last_use[i] < ASSET_EVICT_SECS) continue;
        if (coldest < 0 || asset_last_use[i] < asset_last_use[coldest]) coldest = i;
    }
    if (coldest >= 0) scene_asset_evict(coldest);   // one per frame: eviction is never a stall either
}

// The per-frame asset instance lists are shared with the detail view (detailview.c), which
// stages its own buildings through the same three calls.
void SceneBuilder::assets_begin(void) {
    scene_assets_residency();
    scene_occluded_assets = 0;
    scene_asset_lod_fallbacks = 0;
    for (int i = 0; i < asset_count(); ++i) if (asset_used[i]) { for (int l = 0; l < ASSET_MAX_LODS; ++l) instlist_clear(&asset_inst[i][l]); instlist_clear(&asset_shadow_inst[i]); asset_used[i] = 0; }
    scene_asset_instances = 0;
}
void SceneBuilder::assets_end(void) {
    long long tris = 0;
    for (int i = 0; i < asset_count(); ++i) if (asset_used[i]) {
        const asset_t *a = asset_at(i);
        for (int l = 0; l < ASSET_MAX_LODS; ++l) {
            // Occluded instances (fx1, occquery.h) are moved to the end of the list before its
            // single upload: the main pass draws the visible prefix, and the shadow pass - when
            // this asset has no caster mesh of its own - still draws all of them, since a hidden
            // house casts a visible shadow. Changing mesh_t.ninst between the passes costs
            // nothing; re-uploading the buffer between them is what cost 400 ms a frame once.
            instlist_t *L = &asset_inst[i][l]; int nvis = L->n;
            if (scene_occluded_assets) {
                int w = 0;
                for (int k = 0; k < L->n; ++k) if (L->v[k].fx1 <= 0.f) { if (w != k) { inst_t t = L->v[w]; L->v[w] = L->v[k]; L->v[k] = t; } w++; }
                nvis = w;
            }
            asset_vis[i][l] = nvis;
            mesh_set_instances(&asset_mesh[i][l], L->v, L->n);
            scene_asset_instances += nvis;
            if (l < a->nlods) tris += (long long)(a->lod[l].nidx / 3) * nvis;
        }
        if (shadow_caster_lod && asset_shadow_inst[i].n && asset_shadow_mesh[i].geo.vao)
            mesh_set_instances(&asset_shadow_mesh[i], asset_shadow_inst[i].v, asset_shadow_inst[i].n);
    }
    // closes the director's loop: next frame's thresholds are scaled to meet the tier's budget
    lod_budget_report(tris);
}
// Casters for the shadow cascades: one coarse mesh per asset (see asset_shadow_mesh). Falls
// back to the drawn meshes for an asset whose CPU data was already freed when it was first seen.
int SceneBuilder::assets_submit_shadow(void) {
    int n = 0;
    scene_shadow_tris = 0;
    for (int i = 0; i < asset_count(); ++i) {
        if (!asset_used[i]) continue;
        mesh_t *m = &asset_shadow_mesh[i];
        if (shadow_caster_lod && m->geo.vao && m->ninst) {
            mesh_draw(m); n += m->ninst;
            scene_shadow_tris += (m->geo.nindices / 3) * m->ninst;
            continue;
        }
        for (int l = 0; l < ASSET_MAX_LODS; ++l) if (asset_mesh[i][l].geo.vao) {
            mesh_draw(&asset_mesh[i][l]); n += asset_mesh[i][l].ninst;
            scene_shadow_tris += (asset_mesh[i][l].geo.nindices / 3) * asset_mesh[i][l].ninst;
        }
    }
    return n;
}

int SceneBuilder::assets_submit(void) {
    int n = 0;
    for (int i = 0; i < asset_count(); ++i) if (asset_used[i])
        for (int l = 0; l < ASSET_MAX_LODS; ++l) if (asset_mesh[i][l].geo.vao) {
            mesh_t *m = &asset_mesh[i][l]; int all = m->ninst;
            m->ninst = asset_vis[i][l] < all ? asset_vis[i][l] : all;   // the visible prefix
            mesh_draw(m); n += m->ninst;
            m->ninst = all;
        }
    return n;
}

// ---------------------------------------------------------------------------
// ports (towns are regenerated each frame from a per-port seed: cheap and always in sync)


// per-port cache of the town instances: rebuilt only when what it depends on changes
void SceneBuilder::tc_push(int mesh, inst_t in) { town_cache_t *c = tc_cur; if (c->n % 64 == 0) c->items = (town_item_t *)realloc(c->items, (c->n + 64) * sizeof(town_item_t)); c->items[c->n].mesh = (unsigned char)mesh; c->items[c->n].asset = -1; c->items[c->n].in = in; c->n++; }
void SceneBuilder::tc_push_asset(asset_t *a, inst_t in) { tc_push(TM_ASSET, in); tc_cur->items[tc_cur->n - 1].asset = (short)(a - asset_at(0)); }
unsigned SceneBuilder::town_signature(const port_t *p) {
    unsigned col = p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
    // asset_generation: a town built while its church was still loading is rebuilt once it lands
    return 0x9E3779B1u * (unsigned)(p->size + 1) ^ 0x85EBCA6Bu * (unsigned)(p->building[B_FORT] + 1) ^ 0xC2B2AE35u * (unsigned)(mini((int)(p->pop / 12), 63) + 1) ^ col ^ (unsigned)(p->merged_into >= 0) << 31 ^ 0x27D4EB2Fu * asset_generation();
}

// Town layout. Pieces are placed with a footprint list so they do not stand inside each
// other or in the fort wall, and every house takes the *lowest* ground under its footprint
// and is skipped on steep ground: a house set at its centre height on a slope floats on the
// downhill side and shows a roof without walls on the uphill side (issue-1).
#define TOWN_MAX_FOOTPRINTS 96
#define TOWN_MIN_HEIGHT     0.18f   // above the beach (terrain.c blends sand into grass from 0.15)

int SceneBuilder::fp_free(const footprint_t *fp, int n, float x, float z, float r) {
    for (int i = 0; i < n; ++i) if (dist2d(fp[i].x, fp[i].z, x, z) < fp[i].r + r) return 0;
    return 1;
}
// lowest and highest ground under a square footprint of half size `h` at (x,z)
void SceneBuilder::ground_range(float x, float z, float h, float *lo, float *hi) {
    float c = world_height(x, z); *lo = *hi = c;
    for (int i = 0; i < 4; ++i) {
        float y = world_height(x + (i & 1 ? h : -h), z + (i & 2 ? h : -h));
        if (y < *lo) *lo = y;
        if (y > *hi) *hi = y;
    }
}

// Authored buildings (game/assets, exported by tools/blender_export_glb.py) take the place of
// the procedural boxes wherever a matching role has finished loading; asset_get_role() returns
// NULL for roles that are absent or still in flight, and the town cache's signature carries
// asset_generation(), so a town built from boxes is rebuilt with real houses the moment they
// land. Tiers follow the reference town: tall merchant houses on the square, artisans and
// shops in the middle ring, poor houses and wooden huts by the walls.
static const char *const town_roles_rich[] = { "wealthy_townhouse", "merchant_house", "three_story_townhouse", "inn" };
static const char *const town_roles_mid[]  = { "two_story_townhouse", "artisan_house", "shop", "bakery", "blacksmith", "small_stone_house" };
static const char *const town_roles_poor[] = { "poor_house", "small_wooden_house", "small_stone_house" };
asset_t *SceneBuilder::town_house_asset(float ring, int size, unsigned *seed) {   // ring 0 = square .. 1 = wall
    const char *const *roles; int n;
    float tier = ring + (rnd_f(seed) - 0.5f) * 0.35f;
    if (tier < 0.3f && size >= 2) { roles = town_roles_rich; n = 4; }
    else if (tier < 0.7f)          { roles = town_roles_mid;  n = 6; }
    else                           { roles = town_roles_poor; n = 3; }
    int start = rnd_int(seed, n);
    for (int k = 0; k < n; ++k) { asset_t *a = asset_get_role(ASSET_BUILDING, roles[(start + k) % n]); if (a) return a; }
    return NULL;
}
void SceneBuilder::town_build(const port_t *p, int index) {
    unsigned seed = 1000 + index * 7919;
    float ang = atan2f(p->z - p->lz, p->x - p->lx);
    unsigned col = p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
    footprint_t fp[TOWN_MAX_FOOTPRINTS]; int nfp = 0;
    tc_push(TM_PIER, inst_t{ p->x - cosf(ang)*0.9f, 0, p->z - sinf(ang)*0.9f, -ang, 0.9f, 1,1,1, 0,0,0, 2.0f });
    float fy = world_height(p->lx, p->lz);
    if (p->size <= 0) { tc_push(TM_FLAG, inst_t{ p->lx, fy, p->lz, 0, 1.2f, 0.8f,0.8f,0.8f, 0,0,0,0 }); return; }
    tc_push(TM_FLAG, inst_t{ p->lx + 0.3f, fy, p->lz + 0.3f, 0, 1.0f + 0.2f*p->size, col_r(col), col_g(col), col_b(col), 0,0,0,0 });
    fp[nfp++] = footprint_t{ p->lx + 0.3f, p->lz + 0.3f, 0.08f };
    float rad = 0.55f + 0.3f * p->size;
    int fort = p->building[B_FORT] > 0;
    // the fort wall ring: houses keep a margin inside it so nothing pokes through the stones
    float wall_r = rad + 0.35f, wall_inner = fort ? wall_r * 0.95f - 0.12f : rad + 0.3f;

    // big pieces first: tower on the square, authored church (role "church") landward of it
    if (p->size >= 2) {
        float ts = 0.35f + 0.12f*p->size;
        asset_t *hall = p->size >= 3 ? asset_get_role(ASSET_BUILDING, "town_hall") : NULL;
        if (hall) { tc_push_asset(hall, inst_t{ p->lx, fy - 0.02f, p->lz, -ang + 3.14159f, 1.f, 1,1,1, 0,0,0,0 }); ts = asset_footprint_radius(hall) / 0.45f; }
        else tc_push(TM_TOWER, inst_t{ p->lx, fy - 0.05f, p->lz, 0, ts, 0.8f,0.78f,0.72f, 0,0,0,0 });
        fp[nfp++] = footprint_t{ p->lx, p->lz, ts * 0.45f };
        asset_t *church = asset_get_role(ASSET_BUILDING, "church");
        if (church) {
            float cradius = asset_footprint_radius(church);
            // landward of the tower first, anywhere on the square after a few tries; the church is
            // tall enough to tolerate more slope than a house (it is pivoted at its lowest corner)
            for (int tries = 0; tries < 14; ++tries) {
                float spread = tries < 7 ? 1.4f : 6.2832f;
                float ca = ang + 3.14159f + (rnd_f(&seed) - 0.5f) * spread, cr = ts * 0.45f + cradius + 0.1f + rnd_f(&seed) * 0.25f;
                float cx = p->lx + cosf(ca) * cr, cz = p->lz + sinf(ca) * cr, lo, hi;
                if (cr + cradius > wall_inner || !world_is_land(cx, cz)) continue;
                ground_range(cx, cz, cradius * 0.7f, &lo, &hi);
                if (lo < TOWN_MIN_HEIGHT || hi - lo > 0.28f || !fp_free(fp, nfp, cx, cz, cradius)) continue;
                tc_push_asset(church, inst_t{ cx, lo - 0.02f, cz, -ang, 1.f, 1,1,1, 0,0,0,0 });
                fp[nfp++] = footprint_t{ cx, cz, cradius };
                static int logged; if (!logged++) LOG_INFO("gfx", "first authored church placed at %s (lon %.2f lat %.2f)", p->name, x2lon(cx), z2lat(cz));
                break;
            }
        }
    }

    // houses: as many as the town's size and population call for, where they fit
    int nh = mini(4 + 7 * p->size + (int)(p->pop / 12), 40), placed = 0;
    int med = p->region == REG_MED || p->region == REG_ATLANTIC, colonial = port_colonial(p);
    float roof[3]; if (med) { roof[0]=0.68f; roof[1]=0.32f; roof[2]=0.20f; } else if (colonial) { roof[0]=0.55f; roof[1]=0.42f; roof[2]=0.22f; } else { roof[0]=0.30f; roof[1]=0.32f; roof[2]=0.36f; }
    for (int k = 0; k < nh * 4 && placed < nh && nfp < TOWN_MAX_FOOTPRINTS; ++k) {
        float s = 0.16f + rnd_f(&seed) * 0.12f, half = 0.55f * s;        // roof overhang is the widest part
        float a = rnd_f(&seed) * 6.2832f, ring = sqrtf(rnd_f(&seed)), r = ring * maxf(wall_inner - half, 0.1f);
        float hx = p->lx + cosf(a) * r, hz = p->lz + sinf(a) * r;
        float rot = rnd_f(&seed) * 3.14f, wc = 0.75f + rnd_f(&seed) * 0.2f;
        asset_t *house = town_house_asset(ring, p->size, &seed);
        if (house) { half = asset_footprint_radius(house); s = house->bmax[1] - house->bmin[1]; }
        if (!world_is_land(hx, hz)) continue;
        float lo, hi; ground_range(hx, hz, half, &lo, &hi);
        if (lo < TOWN_MIN_HEIGHT || hi - lo > s * 0.6f) continue;         // beach or too steep
        if (!fp_free(fp, nfp, hx, hz, half * 1.05f)) continue;
        float hy = lo - 0.03f;                                              // the lowest corner touches ground; the rest is buried a little
        if (house) {
            // face the square (doors towards the street), with a little scatter
            tc_push_asset(house, inst_t{ hx, hy, hz, -atan2f(p->lz - hz, p->lx - hx) + (rnd_f(&seed) - 0.5f) * 0.6f, 1.f, 1,1,1, 0,0,0,0 });
        } else {
            float wr = med ? wc : colonial ? wc*0.9f : wc*0.7f, wg = med ? wc*0.92f : colonial ? wc*0.8f : wc*0.55f, wb = med ? wc*0.75f : colonial ? wc*0.6f : wc*0.45f;
            tc_push(TM_HOUSE, inst_t{ hx, hy, hz, rot, s, wr, wg, wb, 0,0,0,0 });
            tc_push(TM_ROOF,  inst_t{ hx, hy + s, hz, rot, s, roof[0], roof[1], roof[2], 0,0,0,0 });
        }
        fp[nfp++] = footprint_t{ hx, hz, half };
        placed++;
    }
    // harbour front: a warehouse landward of the pier, a lighthouse at the shore, when authored
    if (p->size >= 1) {
        asset_t *wh = asset_get_role(ASSET_BUILDING, "harbor_warehouse"); if (!wh) wh = asset_get_role(ASSET_BUILDING, "warehouse");
        asset_t *lh = p->size >= 2 ? asset_get_role(ASSET_BUILDING, "lighthouse") : NULL;
        asset_t *spots[2] = { wh, lh }; float offs[2] = { 0.55f, -0.75f };   // along the shore, either side of the pier
        for (int i = 0; i < 2; ++i) {
            asset_t *a = spots[i]; if (!a) continue;
            float ar = asset_footprint_radius(a);
            float sx = -sinf(ang), sz = cosf(ang);                            // shore direction (perpendicular to land->sea)
            float bx = p->lx + sx * offs[i] + cosf(ang) * 0.15f, bz = p->lz + sz * offs[i] + sinf(ang) * 0.15f, lo, hi;
            if (!world_is_land(bx, bz) || nfp >= TOWN_MAX_FOOTPRINTS) continue;
            ground_range(bx, bz, ar * 0.7f, &lo, &hi);
            if (lo < 0.05f || hi - lo > 0.3f || !fp_free(fp, nfp, bx, bz, ar)) continue;
            tc_push_asset(a, inst_t{ bx, lo - 0.02f, bz, -ang + 3.14159f, 1.f, 1,1,1, 0,0,0,0 });   // facing the water
            fp[nfp++] = footprint_t{ bx, bz, ar };
        }
    }

    if (fort) {
        int nseg = 8 + 2 * p->building[B_FORT]; float wh = 0.22f + 0.05f * p->building[B_FORT];
        for (int k = 0; k < nseg; ++k) {
            float a0 = k * 6.2832f / nseg, a1 = (k+1) * 6.2832f / nseg;
            float x0 = p->lx + cosf(a0)*wall_r, z0 = p->lz + sinf(a0)*wall_r, x1 = p->lx + cosf(a1)*wall_r, z1 = p->lz + sinf(a1)*wall_r, mx = (x0+x1)*0.5f, mz = (z0+z1)*0.5f;
            if (!world_is_land(mx, mz)) continue;
            float lo, hi; ground_range(mx, mz, dist2d(x0,z0,x1,z1) * 0.5f, &lo, &hi);
            if (lo < 0.05f) continue;                                        // no wall on the beach
            tc_push(TM_WALL, inst_t{ mx, lo - 0.1f, mz, -atan2f(z1-z0, x1-x0), wh, 1,1,1, 0,0,0, dist2d(x0,z0,x1,z1) / wh });
        }
    }
}

void SceneBuilder::reset_caches(void) { for (int i = 0; i < MAX_PORTS; ++i) town_cache[i].sig = 0, town_cache[i].n = 0; reset_anim(); lod_reset(); occq_reset(); }   // world/heights may have changed (new game, load)

// `dither` is the town's own mesh-to-icon fade this frame (0 when fully a town)
void SceneBuilder::town_instances(const port_t *p, int index, float dither) {
    town_cache_t *c = &town_cache[index]; unsigned sig = town_signature(p);
    if (c->sig != sig || !c->items) { c->n = 0; c->sig = sig; tc_cur = c; town_build(p, index); }
    static instlist_t *draw_lists[6] = { &houses, &roofs, &towers, &walls, &piers, &flagpoles };
    static instlist_t *cast_lists[6] = { &sh_houses, &sh_roofs, &sh_towers, &sh_walls, &sh_piers, &sh_flagpoles };
    instlist_t **lists = scene_caster_only ? cast_lists : draw_lists;
    for (int i = 0; i < c->n; ++i) {
        const town_item_t *it = &c->items[i];
        if (it->mesh == TM_ASSET) { asset_t *a = asset_at(it->asset); if (a && a->state == ASSET_LOADED) asset_push(a, it->in, lod_key(LODK_TOWN_ITEM, index, i), cam_distance(it->in.x, it->in.z), dither); }
        else { inst_t *in = instlist_push(lists[it->mesh]); *in = it->in; in->dither = dither; }
    }
}

void SceneBuilder::build_ports(void) {
    cam_frustum(frustum);
    scene_culled_ports = scene_occluded_ports = 0;
    int occ = occlusion_active();
    instlist_clear(&houses); instlist_clear(&roofs); instlist_clear(&towers); instlist_clear(&walls); instlist_clear(&piers); instlist_clear(&flagpoles);
    instlist_clear(&sh_houses); instlist_clear(&sh_roofs); instlist_clear(&sh_towers); instlist_clear(&sh_walls); instlist_clear(&sh_piers); instlist_clear(&sh_flagpoles);
    scene_shadow_ports = 0;
    for (int i = 0; i < G.nports; ++i) {
        const port_t *p = &G.ports[i]; if (port_is_gone(p)) continue;
        float d = cam_distance(p->x, p->z), r = 2.5f + 0.5f * p->size;
        float px = cam.mapness > 0.5f ? 0.f : lod_px(r, d);
        lod_pick_t pk = lod_select(lod_key(LODK_PORT, i, 0), town_thr, 2, px, 0);
        port_icon_w[i] = lod_weight(&pk, 1);
        int draws = lod_draws(&pk, 0);                                          // else: icon only
        if (draws && !cam_sphere_visible(frustum, p->x, 0, p->z, r + CULL_MARGIN)) { scene_culled_ports++; draws = 0; }
        if (draws && occ && occ_hidden_sphere(p->x, p->z, r + 1.f, world_height(p->x, p->z) + TOWN_TOP)) { scene_occluded_ports++; draws = 0; }
        if (!draws) {
            // not drawn, but inside the shadow radius: collect its pieces as casters only
            if (shadow_enabled && d < shadow_radius && cam.mapness < 0.5f) {
                scene_caster_only = 1; town_instances(p, i, 0.f); scene_caster_only = 0; scene_shadow_ports++;
            }
            continue;
        }
        town_instances(p, i, lod_dither(&pk, 0));
        int sel = i == SH.sel_port, hov = i == SH.hover_port;
        if (sel || hov) dd_circle(vec3(p->x, 0.1f, p->z), vec3(0,1,0), 2.2f + 0.4f*p->size, dd_opts().with_color(rgb_linear(sel ? rgb(255,240,120) : rgb(220,220,220))));
        if (p->blockaded_by) dd_circle(vec3(p->x, 0.15f, p->z), vec3(0,1,0), 3.5f, dd_opts().with_color(rgb_linear(rgb(255,60,40))).with_dashed(1));
    }
    streamview_push_tiles();   // authored terrain tiles (tile_l*) through the same asset path
    {   // the drawn instances first, then the caster-only ones: one upload, two draw counts
        instlist_t *dl[6] = { &houses, &roofs, &towers, &walls, &piers, &flagpoles };
        instlist_t *cl[6] = { &sh_houses, &sh_roofs, &sh_towers, &sh_walls, &sh_piers, &sh_flagpoles };
        for (int k = 0; k < 6; ++k) { town_vis[k] = dl[k]->n; instlist_append(dl[k], cl[k]); }
    }
    mesh_set_instances(&mesh_pier, piers.v, piers.n); mesh_set_instances(&mesh_house, houses.v, houses.n);
    mesh_set_instances(&mesh_roof, roofs.v, roofs.n); mesh_set_instances(&mesh_tower, towers.v, towers.n);
    mesh_set_instances(&mesh_wall, walls.v, walls.n); mesh_set_instances(&mesh_flagpole, flagpoles.v, flagpoles.n);
}

int SceneBuilder::submit_town_meshes(int shadow) {
    mesh_t *m[6] = { &mesh_house, &mesh_roof, &mesh_tower, &mesh_wall, &mesh_pier, &mesh_flagpole };
    int n = 0;
    for (int k = 0; k < 6; ++k) {
        int all = m[k]->ninst;
        if (!shadow && town_vis[k] < all) m[k]->ninst = town_vis[k];
        mesh_draw(m[k]); n += m[k]->ninst;
        m[k]->ninst = all;
    }
    return n;
}
int SceneBuilder::submit_ports(void) { return submit_town_meshes(0) + assets_submit(); }

// shadow casters: every town piece and every ship (piers shadow the water, flagpoles the
// squares). Gunsmoke is deliberately left out: a hard-edged shadow from a smoke ball looks wrong.
int SceneBuilder::submit_shadow(void) {
    int n = detailview_mode() != DV_OFF ? detailview_submit_shadow()   // the staged scene is the only caster set
                                        : submit_town_meshes(1) + assets_submit_shadow() + submit_ship_meshes(1) + shipyard_submit_shadow();
    return n;
}

// ---------------------------------------------------------------------------
// icons

void SceneBuilder::icon(float x, float z, float heading, float size, int shape, float alpha, float outline, unsigned col) {
    icon_t *ic = icon_push(); *ic = icon_t{ x, 0.3f, z, heading, size, (float)shape, alpha, outline, col_r(col), col_g(col), col_b(col), 0 };
}

// Clustering (DESIGN_LOD.md step 6). Zoomed out to a continent, several hundred markers land on
// top of each other and the map turns into a smear of dots that is both unreadable and a draw
// call per dot. Markers closer together than ICON_CLUSTER_PX are therefore merged into the most
// important one of the group, grown a little to show it stands for several. Selection and hover
// always survive as themselves, so what the player is pointing at never disappears into a blob.
#define ICON_CLUSTER_PX 34.f

// Returns the group this marker belongs to, creating it if the cell is free. `prio` picks the
// representative; -1 means "never cluster this one".
icon_group_t *SceneBuilder::icon_cluster(float sx, float sy, int index, float prio) {
    int cx = (int)floorf(sx / ICON_CLUSTER_PX), cy = (int)floorf(sy / ICON_CLUSTER_PX);
    for (int i = 0; i < n_icon_groups; ++i) {
        icon_group_t *g = &icon_groups[i];
        if (g->cell_x != cx || g->cell_y != cy) continue;
        g->count++;
        if (prio > g->prio) { g->prio = prio; g->best = index; }
        return g;
    }
    if (n_icon_groups >= (int)(sizeof icon_groups / sizeof *icon_groups)) return NULL;
    icon_group_t *g = &icon_groups[n_icon_groups++];
    g->cell_x = cx; g->cell_y = cy; g->best = index; g->count = 1; g->prio = prio;
    return g;
}

void SceneBuilder::push_icons(void) {
    // icon alpha is the mesh-to-icon fade weight from this frame's scene build; when the mesh
    // passes did not run this frame (pass toggles, menu) icons simply draw in full
    int fresh = icon_w_time == RF.time;
    int clustering = lod_tier() >= TIER_STRATEGIC;
    scene_icons_clustered = 0;
    n_icon_groups = 0;
    for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i]; if (port_is_gone(p)) continue;
        float w = fresh ? port_icon_w[i] : 1.f;
        if (w <= 0.002f) continue;
        int important = i == SH.sel_port || i == SH.hover_port;
        int count = 1;
        if (clustering && !important) {
            float sx, sy;
            if (!cam_project(p->x, 0.3f, p->z, &sx, &sy)) continue;
            icon_group_t *g = icon_cluster(sx, sy, i, (float)p->size + (p->nation == G.player ? 0.5f : 0.f));
            if (g && g->best != i) { scene_icons_clustered++; continue; }   // a bigger neighbour speaks for it
            if (g) count = g->count;
        }
        unsigned col = p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
        float sz = p->size <= 0 ? 5.f : 6.f + 2.5f * p->size;
        if (count > 1) sz *= 1.f + 0.16f * logf((float)count) / 0.693f;      // grows with log2 of the group
        icon(p->x, p->z, 0, sz, p->size <= 0 ? ICON_DIAMOND : ICON_DISC, (p->size <= 0 ? 0.8f : 1.f) * w, 1.f, col);
        if (count > 1) icon(p->x, p->z, 0, sz + 7, ICON_RING, 0.55f * w, 0, col);   // a group, not one great city
        if (p->blockaded_by) icon(p->x, p->z, 0, sz + 8, ICON_RING, 0.9f * w, 0, rgb(255,50,25));
        if (p->haven && p->size > 0) icon(p->x, p->z, 0.785f, sz * 0.6f, ICON_CROSS, w, 0, rgb(12,12,12));
        if (important) icon(p->x, p->z, 0, sz + 10, ICON_RING, 0.9f * w, 0, rgb(255,240,128));
    }
    int port_groups = n_icon_groups;   // fleets cluster among themselves, never into a town
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        float w = fresh ? fleet_icon_w[i] : 1.f;
        if (w <= 0.002f) continue;
        float x, z, heading; fleet_pos(f, &x, &z, &heading);
        int sel = f->id == SH.sel_fleet_id, hov = f->id == SH.hover_fleet_id;
        int count = 1;
        if (clustering && !sel && !hov) {
            float sx, sy;
            if (!cam_project(x, 0.8f, z, &sx, &sy)) continue;
            icon_group_t *g = icon_cluster(sx, sy, i, (float)f->nships + (is_player(f->nation) ? 8.f : 0.f));
            if (g && g->best != i) { scene_icons_clustered++; continue; }
            if (g) count = g->count;
        }
        unsigned col = f->role == ROLE_PIRATE ? rgb(25,25,25) : nations_info[f->nation].color;
        float sz = sel ? 13.f : is_player(f->nation) ? 11.f : 9.f;
        if (count > 1) sz *= 1.f + 0.14f * logf((float)count) / 0.693f;
        icon(x, z, heading, sz, ICON_ARROW, w, 1, col);
        if (count > 1) icon(x, z, 0, sz + 6, ICON_RING, 0.5f * w, 0, col);
        if (sel || hov) icon(x, z, 0, 20, ICON_RING, 0.9f * w, 0, rgb(255,240,128));
    }
    (void)port_groups;
    if (SH.show_regions || cam.mapness > 0.4f) for (int i = 0; i < G.ninland; ++i) { const inland_t *s = &G.inland[i]; if (!s->alive) continue;   // mines and farms of the hinterlands
        icon(s->x, s->z, 0, 6 + 2 * minf(s->pop, 4), ICON_SQUARE, 0.95f, 1, goods[s->good].color); }
    if (cam.mapness > 0.4f) for (int i = 0; i < G.nevents; ++i) { const event_t *e = &G.events[i]; if (e->type == EV_STORM) icon(e->x, e->z, RF.time*0.8f, 22, ICON_STORM, 0.9f, 0, rgb(64,64,90)); }
}

// ---------------------------------------------------------------------------
// debug-draw overlays

void SceneBuilder::draw_overlays(void) {
    const fleet_t *f = fleet_by_id(SH.sel_fleet_id);
    if (f && f->at_port < 0 && f->pathi < f->npath) {
        float px = f->x, pz = f->z;
        for (int i = f->pathi; i < f->npath; ++i) { dd_line(vec3(px, 0.15f, pz), vec3(f->path_x[i], 0.15f, f->path_z[i]), dd_opts().with_color(rgb_linear(rgb(255, 240, 140))).with_top(1)); px = f->path_x[i]; pz = f->path_z[i]; }
        if (f->target_port >= 0) dd_circle(vec3(G.ports[f->target_port].x, 0.15f, G.ports[f->target_port].z), vec3(0,1,0), 2.5f, dd_opts().with_color(rgb_linear(rgb(255,240,140))).with_top(1));
    }
    if (f && f->nroute >= 2) for (int i = 0; i < f->nroute; ++i) { const port_t *a = &G.ports[f->route[i]], *b = &G.ports[f->route[(i+1)%f->nroute]];
        if (SH.route_edit && i + 1 == f->nroute) break;
        dd_line(vec3(a->x, 0.2f, a->z), vec3(b->x, 0.2f, b->z), dd_opts().with_color(rgb_linear(SH.route_edit ? rgb(120,255,160) : rgb(120, 220, 255))).with_dashed(!SH.route_edit).with_top(1)); }
    if (SH.show_lanes && cam.mapness > 0.2f)
        for (int i = 0; i < G.nlanes; ++i) { const lane_t *l = &G.lanes[i]; if (l->traffic < 0.8f) continue;
            float t = clampf(l->traffic / 12.f, 0.15f, 1.f); unsigned c = rgb_linear(rgb((unsigned)(90 + 120*t), (unsigned)(60 + 60*t), (unsigned)(30 + 20*t)));
            for (int k = 0; k + 1 < l->npts; ++k) dd_line(vec3(l->x[k], 0.25f, l->z[k]), vec3(l->x[k+1], 0.25f, l->z[k+1]), dd_opts().with_color(c).with_top(1)); }
    for (int i = 0; i < G.nevents; ++i) { const event_t *e = &G.events[i]; if (e->type != EV_STORM) continue;
        float t = RF.time;
        for (int k = 0; k < 3; ++k) dd_circle(vec3(e->x, 0.4f + k*0.3f, e->z), vec3(0,1,0), e->radius * (0.55f + 0.22f*k) + sinf(t*2 + k)*0.5f, dd_opts().with_color(rgb_linear(rgb(70, 70, 90))).with_top(cam.mapness > 0.5f));
        if (cam.mapness < 0.4f) for (int k = 0; k < 24; ++k) { float a = k * 0.2618f + t*0.7f, r = e->radius * (0.3f + 0.6f*((k*7)%5)/5.f), x = e->x + cosf(a)*r, z = e->z + sinf(a)*r;
            dd_line(vec3(x, 3.5f, z), vec3(x - 0.4f, 0.2f, z + 0.3f), dd_opts().with_color(rgb_linear(rgb(120,130,160)))); }
    }
    if (SH.show_wind && cam.mapness < 0.9f) {
        float step = maxf(3, cam.dist * 0.18f + 2), x0 = floorf((cam.tx - step*7) / step) * step, z0 = floorf((cam.tz - step*5) / step) * step;
        for (int j = 0; j < 11; ++j) for (int i = 0; i < 15; ++i) {
            float x = x0 + i*step, z = z0 + j*step; if (world_is_land(x, z)) continue;
            float dx, dz, s; wind_at(x, z, G.day, &dx, &dz, &s);
            float l = step * 0.3f * s, ph = fmodf(RF.time*0.5f + i*0.37f + j*0.61f, 1.f);
            vec3 a = vec3(x + dx*l*(ph-0.5f), 0.6f, z + dz*l*(ph-0.5f)), b = vec3(a.x + dx*l*0.5f, 0.6f, a.z + dz*l*0.5f);
            dd_arrow(a, b, dd_opts().with_color(rgb_linear(rgb(200, 215, 235)))); }
    }
}

} // namespace mare::game::view

// C ABI (scene.h) for the unported callers
extern "C" {
void scene_set_tick(float period_s, float days, float left_s) { mare::game::view::scene_builder().set_tick(period_s, days, left_s); }
float scene_smoke_window(void) { return mare::game::view::scene_builder().smoke_window(); }
int  scene_assets_submit_shadow(void) { return mare::game::view::scene_builder().assets_submit_shadow(); }
int  scene_occlusion_active(void) { return mare::game::view::scene_builder().occlusion_active(); }
size_t scene_asset_gpu_budget(void) { return mare::game::view::scene_builder().asset_gpu_budget(); }
float  scene_asset_idle_s(int asset_index) { return mare::game::view::scene_builder().asset_idle_s(asset_index); }
int scene_asset_lod_count(int asset_index, int lod) { return mare::game::view::scene_builder().asset_lod_count(asset_index, lod); }
struct mesh_t *scene_asset_mesh(struct asset_t *a, int lod) { return mare::game::view::scene_builder().asset_gpu_mesh(a, lod); }
void scene_asset_upload(struct asset_t *a, int lod) { mare::game::view::scene_builder().asset_upload(a, lod); }
void scene_assets_begin(void) { mare::game::view::scene_builder().assets_begin(); }
void scene_asset_instance(struct asset_t *a, inst_t in, unsigned key, float camdist) { mare::game::view::scene_builder().asset_instance(a, in, key, camdist); }
void scene_assets_end(void) { mare::game::view::scene_builder().assets_end(); }
int  scene_assets_submit(void) { return mare::game::view::scene_builder().assets_submit(); }
void scene_fleet_pos(const fleet_t *f, float *x, float *z, float *heading) { mare::game::view::scene_builder().fleet_pos(f, x, z, heading); }
void scene_reset_caches(void) { mare::game::view::scene_builder().reset_caches(); }
void scene_reset_anim(void) { mare::game::view::scene_builder().reset_anim(); }
void scene_build_ports(void) { mare::game::view::scene_builder().build_ports(); }
void scene_build_ships(void) { mare::game::view::scene_builder().build_ships(); }
int  scene_submit_ports(void) { return mare::game::view::scene_builder().submit_ports(); }
int  scene_submit_ships(void) { return mare::game::view::scene_builder().submit_ships(); }
int  scene_submit_smoke(void) { return mare::game::view::scene_builder().submit_smoke(); }
int  scene_submit_shadow(void) { return mare::game::view::scene_builder().submit_shadow(); }
void scene_push_icons(void) { mare::game::view::scene_builder().push_icons(); }
void scene_draw_overlays(void) { mare::game::view::scene_builder().draw_overlays(); }
}
