// Detail view (see staged_scene.h). Everything here is presentation: the scenes are built from
// the simulation state at entry (engaged fleets, port size/buildings/population) and then live
// their own gentle life - ships sail, turn and trade broadsides, flags and smoke move - without
// writing anything back.
//
// Local scale: 1 unit ~ 4 m. Ships reuse the world meshes at DV_SHIP_SCALE, which puts a sloop
// at ~24 m and a ship of the line at ~52 m; the battle coast sits DV_COAST_DIST (~1 km) away.
// mare::game::view::DetailView: the staged-scene state machine (`dv`) and its battle/city
// builders behind staged_scene.h. The C ABI is a set of shims over the singleton at the bottom.
#include "../platform/v3cpp.hh"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "staged_scene.h"
#include "render.h"
#include "camera.h"
#include "scene.h"
#include "glsl.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/diag.h"
#include "../assets/asset.h"
#include "../data/tables.h"
#include "../sim/fleet.h"
#include "../sim/settlement.h"
#include "lod.h"
#include "shipyard.h"
#include "particles.h"
#include "wake.h"
#include "../sim/naval_physics.h"
#include "../world/wind.h"
#include "../world/world.h"
#include "../world/terrain_chunks.h"
#include "../engine/shaders.h"
}

namespace mare::game::view {

// the round shot in the air (0.22.14): one mesh, instanced per ball each frame (frame())
static mesh_t shot_mesh; static instlist_t shot_il; static int shot_built;


#define DV_SEA_AMP      0.60f        // staged wave height, relative to the map's (the sea is stretched x DV_SHIP_SCALE); 0.22.11: 0.45 -> 0.60 (issue8 "a little bigger amplitude")
#define DV_SHIP_SCALE   9.f          // world ship mesh -> local units (1 unit ~ 2.8 m: a ship of the line is ~19 units)
#define DV_COAST_DIST 190.f          // battle: coast this far north (+z is south, so -z)
#define DV_MAX_SHIPS   50
#define DV_MAX_BUILD  160
#define DV_SEA_DEPTH   40.f          // staged scenes have no heightmap: open water everywhere
typedef struct dv_build_t { short asset; inst_t in; } dv_build_t;   // authored houses (registry index + local instance)
#define DV_CLOUD_IN   0.8f           // seconds of cloud cover building up
#define DV_CLOUD_OUT  1.1f           // seconds of reveal (the camera dives during this)

enum { DVP_IDLE, DVP_IN, DVP_SHOW, DVP_OUT_IN, DVP_OUT };  // transition phases: IN -> (swap) -> SHOW; exit: OUT_IN -> (restore) -> OUT

typedef struct dv_ship_t {
    int   type, side;
    float x, z, heading, speed, phase;   // speed: local units per second, from the class stats
    float r, g, b;
    float next_fire;
    int   nation;                    // picks the authored class flavour (shipyard.h)
    float course;                    // the course she holds (the heading weaves about it)
    float turn;                      // rate of turn, radians per second (class stats)
    float len;                       // hull length, local units (foam and bow wave)
    int   slot;                      // place in her side's line (0 = leads)
    int   guns;                      // the class's guns (the broadside's particles, 0.22.7)
    int   oared;                     // under oars: the wind does not set her pace
    int   body;                      // her rigid body in the tactical world (sim/naval_physics.h, 0.22.9); -1 in the harbour
    float beam, tonnes;              // what that body is built from
    float oar_speed;                 // 0.22.15: under oars, local units per second (0 = no oars)
    float furl;                      // 0.22.15: 0..1, how far the sails are taken in (an oared ship pulling into the wind)
    int   src_fleet, src_slot;       // 0.22.19 (todo 153): the strategic ship she stands for; -1 in a staged exercise
    float fire_puff;                 // 0.22.19 (todo 152): seconds to the next plume off a burning ship
    float mast_fall[4];              // 0.22.23 (issue19): seconds since each station was carried away, <0 standing
} dv_ship_t;

// Metres per local unit: the authored hulls are fitted at SHIP_M_PER_UNIT (0.045 world units
// per metre, shipyard.c) and the staged scenes draw them DV_SHIP_SCALE larger.
#define DV_M_PER_UNIT  (1.f / (0.045f * DV_SHIP_SCALE))
#define DV_KNOT        (0.5144f / DV_M_PER_UNIT)   // one knot, local units per second

// The staged shore (0.22.7, issue4): the battle's coast is read from the world at the battle's
// position - its bearing, how far off it lies, the land cover and the relief behind it - and
// built as a heightfield with a beach profile the sea shader shares (sea_local_shore_*), so the
// breakers run up it. No land within DV_COAST_PROBE world units = a battle in the open sea.
#define DV_COAST_PROBE   8.f     // world units (~56 km) searched for land
#define DV_SHORE_AMP    14.f     // shoreline wiggle amplitude, local units
#define DV_SHORE_FREQ  0.011f    // its base frequency
#define DV_SHORE_SLOPE  0.15f    // seabed slope: depth per local unit seaward (surf band 7..33 units off the sand)
#define DV_LOCAL_M      2.47f    // metres per local unit (DV_M_PER_UNIT below, spelled out for the landscape)
#define DV_WIND_KN     14.f      // knots per unit of wind strength (wind_at gives ~0.3..1.4)
enum { DV_RELIEF_LOW = 0, DV_RELIEF_HILLS, DV_RELIEF_MOUNTAINS };
enum { DV_LAND_GREEN = 0, DV_LAND_FOREST, DV_LAND_SCRUB, DV_LAND_DESERT, DV_LAND_MARSH, DV_LAND_SNOW };

// C++ has no (float[]){...}: v3f_t{a, b, c}.v is the temporary array the mesh builder takes.
struct v3f_t { float v[3]; };

static const char *const dv_roles_rich[] = { "wealthy_townhouse", "merchant_house", "three_story_townhouse", "inn" };
static const char *const dv_roles_mid[]  = { "two_story_townhouse", "artisan_house", "shop", "bakery", "blacksmith", "small_stone_house" };
static const char *const dv_roles_poor[] = { "poor_house", "small_wooden_house", "small_stone_house" };

class DetailView {
public:
    int  mode(void);                    // DV_OFF when inactive (world renders as usual)
    int  busy(void);                    // clouds are covering the screen (input besides Esc ignored)
    void enter_battle(int fleet_id);
    void enter_city(int port);
    void exit(void);
    void abort(void);
    void frame(float dt);
    int  submit(void);
    int  submit_shadow(void);
    int  submit_smoke(void);
    void sea(void);
    void clouds(void);
    // the battle clock, the wind and the coast (0.22.7)
    float timescale(void) { return dv.timescale; }
    void  set_timescale(float s) { dv.timescale = clampf(s, 0.f, 8.f); }
    float clock_scale(void) { return mode() == DV_BATTLE ? dv.timescale : 1.f; }
    void  wind(float *dx, float *dz, float *str) { *dx = dv.wdx; *dz = dv.wdz; *str = dv.wstr; }
    int   show_wind(void) { return dv.show_wind; }
    void  toggle_wind(void) { dv.show_wind ^= 1; }
    const char *coast_desc(void) { return dv.coast_desc; }
    int   hits(void) { return dv.hits; }
    void  overlays(void);
    void  weather(void);
    float eye_floor(void);
    void  trailer(float t);              // the recorder's clock (seconds into the take; < 0 = holding the first mark)
    // 0.22.17 (issue14 "Q and E in battle view don't work" - they had never been bound to
    // anything). The player's side under manual steering: `helm(d)` takes the helm if it is not
    // already taken and swings the ordered course by d radians, `helm_release()` hands her back
    // to the line. Every ship answers at the rate her class and displacement allow.
    void  helm(float d);
    void  helm_release(void);
    int   helm_on(void) const { return helm_on_; }
    float helm_course(void) const { return helm_course_; }
    int   ship_count(void) const { return dv.nships; }
    int   ship_info(int i, dv_shipinfo_t *out) const {
        if (i < 0 || i >= dv.nships || !out) return 0;
        const dv_ship_t *s = &dv.ship[i];
        *out = dv_shipinfo_t{};
        out->side = s->side == helm_side_ ? 0 : 1; out->type = s->type;
        out->hull = out->sails = out->guns = 1.f;
        if (s->body >= 0) {
            nphys_damage_t d; nphys_damage_get(s->body, &d);
            out->sunk = d.sunk; out->stranded = d.stranded; out->masts_down = d.masts_down;
            out->hull = 1.f - d.hull; out->sails = 1.f - d.rig; out->guns = 1.f - d.guns;
            out->water = d.flood; out->fire = d.fire;
            out->crew = d.crew; out->ammo = d.ammo;        // 0.22.22 (todo 178)
        }
        return 1;
    }
    int   fire_order(void) const { return fire_order_; }
    int   move_order(void) const { return move_order_; }
    int   furl_order(void) const { return furl_order_; }
    void  set_furl_order(int o) { if ((o != 0) != (furl_order_ != 0)) {
              furl_order_ = o != 0;
              LOG_INFO("gfx", "battle order: %s", furl_order_ ? "take in sail" : "make sail"); } }
    void  set_fire_order(int o) { if (o >= DV_FIRE_NEAREST && o <= DV_FIRE_TRADERS && o != fire_order_) {
              fire_order_ = o;
              LOG_INFO("gfx", "battle order: fire at %s", o == DV_FIRE_WARSHIPS ? "the heaviest ship in range" : o == DV_FIRE_TRADERS ? "the lightest ship in range (the convoy, not the escort)" : "the nearest enemy"); } }
    void  set_move_order(int o) { if (o >= DV_MOVE_LINE && o <= DV_MOVE_LAST_STAND && o != move_order_) {
              move_order_ = o;
              if (o != DV_MOVE_LINE) helm_on_ = 0;      // an order supersedes the hand on the wheel
              LOG_INFO("gfx", "battle order: %s", o == DV_MOVE_BOARD ? "close and board" : o == DV_MOVE_RETREAT ? "haul off and run" : o == DV_MOVE_LAST_STAND ? "hold station - no quarter asked" : "back into the line"); } }

private:
    void stage_free(void);
    void stage_battle(void);
    void stage_city(void);
    void battle_add_ship(int type, int side, float x, float z, float heading, unsigned col, unsigned *seed, int nation, int src_fleet, int src_slot);
    void write_back(void);      // 0.22.19 (todo 153): the butcher's bill, into the strategic ships
    static float dv_line_perim(void);
    static void  dv_line_point(int side, float sarc, float *x, float *z, float *hdg);
    void battle_form_lines(void);
    void build_battle(int fleet_id);
    void battle_update(float dt);
    static float dv_surface(float x, float z, void *self);
    void probe_coast(void);
    float shore_wiggle(float u) const;
    float stage_height(float u, float v, float *slope_out) const;
    void stage_colour(float u, float v, float h, float slope, float *rgb) const;
    void stage_trees(mb_t *m, unsigned *seed);
    static asset_t *dv_house_asset(float ring, int size, unsigned *seed);
    int  dv_free(float x, float z, float r);
    void dv_take(float x, float z, float r);
    void build_city(int port);
    void dv_begin(int mode, int port, int fleet);
    float dv_cover(void);
    void dv_sea_params(void);

    // ---- state (file-scope statics of the C version)
    struct {
        int mode, want_mode;             // active mode and the mode being faded in
        int phase; float t;              // transition phase + its clock
        float cx, cz;                    // scene centre (world coords, world passes are off anyway)
        float saved_tx, saved_tz, saved_dist, saved_yaw;
        int want_port, want_fleet;
        // battle
        dv_ship_t ship[DV_MAX_SHIPS]; int nships;
        float line_s[2], line_v[2], line_gap[2]; int nside[2];   // per side: line head's arc position, line speed, spacing
        float time;
        float timescale;                 // the battle clock (0.22.7): 1 = real time, 0 = stopped; the strategic clock is separate
        int   hits;                      // round shot that struck a hull this battle (ballistics, 0.22.11)
        int   show_wind;                 // wind arrows over the water
        float wdx, wdz, wstr;            // the wind at the battle's position (world direction it blows towards, strength)
        // the coast read from the world (probe_coast)
        int   coast_on;                  // 0 = open sea (no stage coast, haze at the horizon)
        float cnx, cnz;                  // unit normal from the sea towards the land (world xz)
        float cdist_w;                   // distance to the nearest land, world units
        float coast_dist;                // where the stage shoreline stands, local units from the scene centre
        int   land, relief;              // DV_LAND_*, DV_RELIEF_*
        float relief_m;                  // highest ground sampled behind the coast, metres
        float lat;                       // the battle's latitude (pines north, palms south)
        int   coast_from_data;           // the terrain package answered the probe (else world_height stood in and the probe is retried while the chunks page in)
        float reprobe_t;                 // scene time of the next retry
        char  coast_desc[64];            // for the HUD
        // stage + instance lists
        mesh_t stage; int stage_built;
        dv_build_t build[DV_MAX_BUILD]; int nbuild;   // authored houses (registry index + local instance)
        instlist_t ships_il[SHIP_COUNT], houses, roofs, towers, walls, piers, flags;
        // clouds
        rhi_pipeline_t cloud_prg; rhi_geometry_t cloud_geo; int cloud_init;
    } dv;
    float dv_fp[220][3]; int dv_nfp;
    int   helm_on_ = 0, helm_side_ = 0;   // 0.22.17: the manual helm
    float helm_course_ = 0.f;
    int   fire_order_ = DV_FIRE_NEAREST, move_order_ = DV_MOVE_LINE;   // 0.22.19: the player's orders
    int   furl_order_ = 0;                                              // 0.22.20 (issue16): the player's side takes in sail
};

DetailView &detail_view() { static DetailView d; return d; }

// The world keeps rendering while the clouds roll in over the map (IN) and after the camera is
// restored (OUT); the staged scene owns the frame during SHOW and the covering half of the exit.
int DetailView::mode(void) { return dv.phase == DVP_SHOW || dv.phase == DVP_OUT_IN ? dv.mode : DV_OFF; }
int DetailView::busy(void) { return dv.phase == DVP_IN || dv.phase == DVP_OUT_IN; }

// ---------------------------------------------------------------------------
// stage meshes (ground, sea, coast), built with the ordinary mesh builder so they receive
// shadows and PBR from the standard pipeline

void DetailView::stage_free(void) { dv.stage_built = 0; }   // geometry is leaked per entry (tiny); rebuilt each time

// ---- the coast, read from the world (0.22.7, issue4)

// Nearest land to the scene centre on rings out to DV_COAST_PROBE world units; then the land
// cover and the relief behind it from the terrain package (world_height as the fallback).
void DetailView::probe_coast(void) {
    dv.coast_on = 0; dv.cnx = 0; dv.cnz = -1; dv.cdist_w = 1e9f; dv.land = DV_LAND_GREEN; dv.relief = DV_RELIEF_LOW; dv.relief_m = 0;
    dv.lat = z2lat(dv.cz);
    float best = 1e9f, bx = 0, bz = -1;
    for (float r = 0.5f; r <= DV_COAST_PROBE && best > 1e8f; r += 0.25f)
        for (int k = 0; k < 48; ++k) {
            float a = k * 6.2831853f / 48.f, x = dv.cx + cosf(a) * r, z = dv.cz + sinf(a) * r;
            if (world_is_land(x, z) && r < best) { best = r; bx = cosf(a); bz = sinf(a); }
        }
    if (best > 1e8f) { snprintf(dv.coast_desc, sizeof dv.coast_desc, "open sea, no land within %.0f km", DV_COAST_PROBE * 7.f); LOG_INFO("gfx", "battle coast: %s", dv.coast_desc); return; }
    dv.coast_on = 1; dv.cnx = bx; dv.cnz = bz; dv.cdist_w = best;
    dv.coast_dist = 140.f + 70.f * clampf(best / 4.f, 0.f, 1.f);
    // land cover and relief: a few samples inland from the hit, the package first
    int votes[6] = {0}; float hmax = 0; int nsamp = 0, ndata = 0;
    static const float steps[5] = { 0.12f, 0.4f, 0.8f, 1.4f, 2.2f };
    for (int i = 0; i < 5; ++i) for (int j = -1; j <= 1; ++j) {
        float x = dv.cx + bx * (best + steps[i]) + -bz * j * 0.5f, z = dv.cz + bz * (best + steps[i]) + bx * j * 0.5f;
        if (!world_is_land(x, z)) continue;
        gc_sample_t gs; float hm = world_height(x, z) * (9000.f / 16.f); int cls = 0;
        if (geochunks_sample(x2lon(x), z2lat(z), &gs)) { hm = maxf(gs.height_m, 0.f); if (gs.has_maps) cls = gs.biome; ndata++; }
        hmax = maxf(hmax, hm); nsamp++;
        int land = cls == 10 ? DV_LAND_FOREST : cls == 20 ? DV_LAND_SCRUB : cls == 60 ? DV_LAND_DESERT : (cls == 90 || cls == 95) ? DV_LAND_MARSH
                 : (cls == 70 || cls == 100) ? DV_LAND_SNOW : DV_LAND_GREEN;
        if (cls == 0) land = (dv.lat < 34.f && x2lon(x) > -10.f) ? DV_LAND_DESERT : DV_LAND_GREEN;   // no maps: the Maghreb and the Levant are dry, the rest green
        votes[land] += i < 2 ? 2 : 1;   // the near samples count double: the beach is what the camera sees
    }
    int bestv = 0; for (int i = 1; i < 6; ++i) if (votes[i] > votes[bestv]) bestv = i;
    dv.land = bestv; dv.relief_m = hmax; dv.coast_from_data = ndata * 2 >= nsamp;
    dv.relief = hmax < 80.f ? DV_RELIEF_LOW : hmax < 450.f ? DV_RELIEF_HILLS : DV_RELIEF_MOUNTAINS;
    static const char *const land_names[6] = { "green", "wooded", "scrub", "desert", "marsh", "snowy" };
    static const char *const relief_names[3] = { "lowland", "hills", "mountains" };
    static const char *const compass[16] = { "N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW" };
    float brg = atan2f(bx, -bz) * 57.2958f; if (brg < 0) brg += 360.f;
    snprintf(dv.coast_desc, sizeof dv.coast_desc, "%s %s to the %s, %.0f km", land_names[dv.land], relief_names[dv.relief], compass[(int)((brg + 11.25f) / 22.5f) % 16], best * 7.f);
    LOG_INFO("gfx", "battle coast: %s (%d land samples, %d from the package, highest %.0f m)", dv.coast_desc, nsamp, ndata, hmax);
}

// the shoreline's wander along the coast: the same three sines the sea shader evaluates
float DetailView::shore_wiggle(float u) const {
    return DV_SHORE_AMP * (sinf(u * DV_SHORE_FREQ) + 0.5f * sinf(u * DV_SHORE_FREQ * 2.3f + 1.7f) + 0.25f * sinf(u * DV_SHORE_FREQ * 5.1f + 0.4f));
}

// Height of the stage at (u along the shore, v inland from the nominal shoreline), local units.
// Seaward: the beach profile the sea shares. Landward: the beach, then relief from a noise
// whose amplitude and base slope follow what the data said stands behind this coast.
float DetailView::stage_height(float u, float v, float *beach_w_out) const {
    float ve = v - shore_wiggle(u);
    float beach_w = dv.land == DV_LAND_DESERT ? 26.f : dv.land == DV_LAND_MARSH ? 0.f : dv.relief == DV_RELIEF_MOUNTAINS ? 7.f : 16.f;
    if (beach_w_out) *beach_w_out = beach_w;
    if (ve < 0.f) return maxf(ve * DV_SHORE_SLOPE, -DV_SEA_DEPTH);
    float beach_slope = dv.land == DV_LAND_MARSH ? 0.012f : 0.055f;
    float hb = minf(ve, beach_w) * beach_slope;
    float x = maxf(ve - beach_w, 0.f);
    float ramp = clampf(x / 140.f, 0.f, 1.f); ramp = ramp * ramp * (3.f - 2.f * ramp);
    float amp, base, fs;
    switch (dv.relief) {
        default:
        case DV_RELIEF_LOW:       amp = 6.f;   base = 0.006f;  fs = 0.0045f; break;
        case DV_RELIEF_HILLS:     amp = 34.f;  base = 0.030f;  fs = 0.0038f; break;
        case DV_RELIEF_MOUNTAINS: amp = 75.f;  base = 0.075f;  fs = 0.0026f; break;
    }
    if (dv.land == DV_LAND_DESERT && dv.relief == DV_RELIEF_LOW) { amp = 5.f; fs = 0.007f; }    // dunes
    if (dv.land == DV_LAND_MARSH) { amp = minf(amp, 1.5f); base = minf(base, 0.002f); }
    float n = world_fbm(u * fs + 3.1f, x * fs + 7.7f, dv.relief == DV_RELIEF_MOUNTAINS ? 5 : 4, 1711);
    if (dv.relief == DV_RELIEF_MOUNTAINS) n = powf(n, 1.6f) * 1.3f;   // ridges, not rolling
    // the berm behind the beach - dunes on a sandy shore, a low bank elsewhere - so a flat coast
    // still draws a line against the sea from the deck
    float berm = dv.land == DV_LAND_MARSH ? 0.f : (dv.land == DV_LAND_DESERT || dv.relief == DV_RELIEF_LOW ? 2.6f : 1.2f) * (0.7f + 0.6f * world_fbm(u * 0.03f, 1.f, 2, 515));
    float bermk = x < 30.f ? (x / 30.f) * (1.f - x / 30.f) * 4.f : expf(-(x - 30.f) * 0.04f) * 0.0f;
    return hb + berm * bermk + ramp * (amp * n + base * x);
}

void DetailView::stage_colour(float u, float v, float h, float slope, float *rgb) const {
    float ve = v - shore_wiggle(u), beach_w; stage_height(u, v, &beach_w);
    float r, g, b;
    if (ve < 0.f) {   // seabed: pale sand shelving into the dark
        float d = clampf(-h / DV_SEA_DEPTH, 0.f, 1.f);
        r = lerpf(0.62f, 0.25f, d); g = lerpf(0.56f, 0.28f, d); b = lerpf(0.42f, 0.26f, d);
    } else if (ve < beach_w) {
        if (dv.relief == DV_RELIEF_MOUNTAINS) { r = 0.55f; g = 0.52f; b = 0.47f; }           // shingle
        else if (dv.land == DV_LAND_MARSH) { r = 0.42f; g = 0.40f; b = 0.30f; }              // mud
        else { r = 0.80f; g = 0.73f; b = 0.55f; }                                             // sand
    } else {
        float x = ve - beach_w;
        float patch = world_fbm(u * 0.02f + 11.f, x * 0.02f + 5.f, 2, 919);
        switch (dv.land) {
            default:
            case DV_LAND_GREEN:  r = 0.30f; g = 0.46f; b = 0.20f;
                                 if (dv.relief != DV_RELIEF_MOUNTAINS && x < 520.f && patch > 0.60f) { r = 0.56f; g = 0.50f; b = 0.28f; }   // fields
                                 break;
            case DV_LAND_FOREST: r = 0.18f; g = 0.32f; b = 0.14f; break;
            case DV_LAND_SCRUB:  r = 0.42f; g = 0.46f; b = 0.26f; break;
            case DV_LAND_DESERT: { float t = clampf(patch * 1.5f - 0.3f, 0.f, 1.f); r = lerpf(0.62f, 0.80f, t); g = lerpf(0.50f, 0.68f, t); b = lerpf(0.34f, 0.46f, t); } break;
            case DV_LAND_MARSH:  if (patch > 0.6f) { r = 0.30f; g = 0.38f; b = 0.40f; } else { r = 0.34f; g = 0.44f; b = 0.28f; } break;
            case DV_LAND_SNOW:   r = 0.55f; g = 0.56f; b = 0.50f; break;
        }
        // rock on the steep ground, snow on the high
        float rock = clampf((slope - 0.45f) / 0.25f, 0.f, 1.f);
        if (dv.land == DV_LAND_DESERT) { r = lerpf(r, 0.60f, rock); g = lerpf(g, 0.48f, rock); b = lerpf(b, 0.36f, rock); }
        else { r = lerpf(r, 0.46f, rock); g = lerpf(g, 0.43f, rock); b = lerpf(b, 0.40f, rock); }
        float snowline = dv.relief == DV_RELIEF_MOUNTAINS ? (dv.lat > 55.f ? 45.f : 70.f) : dv.land == DV_LAND_SNOW ? 2.f : 1e9f;
        float snow = clampf((h - snowline) / 12.f, 0.f, 1.f) * (1.f - 0.6f * rock);
        r = lerpf(r, 0.92f, snow); g = lerpf(g, 0.93f, snow); b = lerpf(b, 0.95f, snow);
    }
    rgb[0] = r; rgb[1] = g; rgb[2] = b;
}

// Trees as low-poly cones in the stage frame: pines (one tall cone) north of 46 degrees, on the
// uplands and a third of the time anywhere; broadleaves (two cones base to base) elsewhere.
// Dense in the wooded biome, in clumps on the green, thin on the scrub, a few on the marsh.
void DetailView::stage_trees(mb_t *m, unsigned *seed) {
    int want = dv.land == DV_LAND_FOREST ? 1400 : dv.land == DV_LAND_GREEN ? 520 : dv.land == DV_LAND_SCRUB ? 380 : dv.land == DV_LAND_MARSH ? 120 : 0;
    if (dv.land == DV_LAND_DESERT && dv.lat < 36.f) want = 90;   // a few palms along the shore
    float tgx = -dv.cnz, tgz = dv.cnx, D = dv.coast_dist;
    int placed = 0;
    for (int tries = 0; tries < want * 4 && placed < want; ++tries) {
        float u = (rnd_f(seed) - 0.5f) * 1500.f, beach_w;
        float v = shore_wiggle(u) + 2.f + rnd_f(seed) * (dv.land == DV_LAND_DESERT ? 60.f : 430.f);
        float h = stage_height(u, v, &beach_w);
        if (v - shore_wiggle(u) < beach_w + 2.f) continue;
        float e = 3.f, hx = stage_height(u + e, v, 0), hz = stage_height(u, v + e, 0);
        float slope = sqrtf((hx - h) * (hx - h) + (hz - h) * (hz - h)) / e;
        if (slope > 0.6f) continue;
        if (dv.relief == DV_RELIEF_MOUNTAINS && h > (dv.lat > 55.f ? 40.f : 62.f)) continue;   // above the tree line
        if (dv.land == DV_LAND_GREEN && world_fbm(u * 0.012f + 1.f, v * 0.012f + 2.f, 2, 313) < 0.56f) continue;   // clumps
        if (dv.land == DV_LAND_MARSH && world_fbm(u * 0.02f, v * 0.02f, 2, 919) > 0.6f) continue;   // not in the pools
        int pine = dv.land != DV_LAND_DESERT && (dv.lat > 46.f || h > 20.f || rnd_f(seed) < 0.33f);
        float sc = 0.8f + rnd_f(seed) * 0.5f;
        float ht = pine ? 5.5f * sc : 3.6f * sc, rad = pine ? 1.5f * sc : 2.3f * sc;
        if (dv.land == DV_LAND_SCRUB) { ht *= 0.55f; rad *= 0.7f; }
        if (dv.land == DV_LAND_DESERT) { ht = 5.f * sc; rad = 2.4f * sc; }   // the palm: a crown on a stick
        float cr = pine ? 0.12f : 0.20f, cg = pine ? 0.24f : 0.36f, cb = pine ? 0.12f : 0.16f;
        float var = (rnd_f(seed) - 0.5f) * 0.08f; cr += var; cg += var * 1.3f;
        if (dv.land == DV_LAND_DESERT) { cr = 0.30f; cg = 0.42f; cb = 0.18f; }
        // stage frame -> local scene coords
        float px = tgx * u + dv.cnx * (D + v), pz = tgz * u + dv.cnz * (D + v), base = h - 0.2f;
        const int N = 6;
        if (dv.land == DV_LAND_DESERT) {   // trunk + a flat crown of blades
            mb_box(m, px, base + ht * 0.5f, pz, 0.12f, ht * 0.5f, 0.12f, 0.45f, 0.36f, 0.26f, 0);
            for (int k = 0; k < N; ++k) { float a0 = k * 6.2832f / N, a1 = (k + 1) * 6.2832f / N;
                mb_tri(m, px, base + ht, pz, px + cosf(a0) * rad, base + ht - 1.2f, pz + sinf(a0) * rad, px + cosf(a1) * rad, base + ht - 1.2f, pz + sinf(a1) * rad, cr, cg, cb, 0); }
        } else if (pine) {
            for (int k = 0; k < N; ++k) { float a0 = k * 6.2832f / N, a1 = (k + 1) * 6.2832f / N;
                mb_tri(m, px + cosf(a0) * rad, base + ht * 0.12f, pz + sinf(a0) * rad, px + cosf(a1) * rad, base + ht * 0.12f, pz + sinf(a1) * rad, px, base + ht, pz, cr, cg, cb, 0); }
        } else {
            float mid = base + ht * 0.45f;
            for (int k = 0; k < N; ++k) { float a0 = k * 6.2832f / N, a1 = (k + 1) * 6.2832f / N;
                mb_tri(m, px + cosf(a0) * rad, mid, pz + sinf(a0) * rad, px + cosf(a1) * rad, mid, pz + sinf(a1) * rad, px, base + ht, pz, cr, cg, cb, 0);
                mb_tri(m, px + cosf(a1) * rad, mid, pz + sinf(a1) * rad, px + cosf(a0) * rad, mid, pz + sinf(a0) * rad, px, base + ht * 0.1f, pz, cr * 0.8f, cg * 0.8f, cb * 0.8f, 0); }
        }
        placed++;
    }
    LOG_INFO("gfx", "battle stage: %d trees", placed);
}

// The battle stage (0.22.7): a seabed plane forty units down (what the water refracts), and -
// when the probe found land - a heightfield coast at the real bearing: beach profile under the
// water, beach, relief behind, coloured by the land cover, trees on it. The sea pass draws the
// water itself with the same shore profile (sea_local_shore_*), so the surf runs up this sand.
void DetailView::stage_battle(void) {
    mb_t m = {0};
    mb_material(&m, MB_STONE);
    probe_coast();
    const float S = 1700.f, yb = -DV_SEA_DEPTH;
    mb_set_normal(&m, 0, 1, 0, 1.f);
    mb_quad(&m, v3f_t{-S,yb,S}.v, v3f_t{S,yb,S}.v, v3f_t{S,yb,-S}.v, v3f_t{-S,yb,-S}.v, 0.25f, 0.28f, 0.26f, 0);   // the seabed
    if (dv.coast_on) {
        // row spacing: coarse out at sea, fine through the surf and the beach, coarse inland
        static float vs[96]; int nv = 0;
        for (float v = -320.f; v < -60.f; v += 40.f) vs[nv++] = v;
        for (float v = -60.f; v < 60.f; v += 6.f) vs[nv++] = v;
        for (float v = 60.f; v <= 1000.f + 1e-3f; v += 20.f) vs[nv++] = v;
        const int NU = 129; const float u0 = -1600.f, du = 3200.f / (NU - 1);
        static float H[96][NU]; static float C[96][NU][3];
        float tgx = -dv.cnz, tgz = dv.cnx, D = dv.coast_dist;
        for (int j = 0; j < nv; ++j) for (int i = 0; i < NU; ++i) H[j][i] = stage_height(u0 + i * du, vs[j], 0);
        for (int j = 0; j < nv; ++j) for (int i = 0; i < NU; ++i) {
            int i0 = i > 0 ? i - 1 : i, i1 = i < NU - 1 ? i + 1 : i, j0 = j > 0 ? j - 1 : j, j1 = j < nv - 1 ? j + 1 : j;
            float su = (H[j][i1] - H[j][i0]) / ((i1 - i0) * du), sv = (H[j1][i] - H[j0][i]) / maxf(vs[j1] - vs[j0], 1e-3f);
            stage_colour(u0 + i * du, vs[j], H[j][i], sqrtf(su * su + sv * sv), C[j][i]);
        }
        for (int j = 0; j + 1 < nv; ++j) for (int i = 0; i + 1 < NU; ++i) {
            float P[4][3]; float col[3] = {0,0,0}; float nx = 0, ny = 0, nz = 0;
            const int ii[4] = { i, i+1, i+1, i }, jj[4] = { j, j, j+1, j+1 };
            for (int k = 0; k < 4; ++k) {
                float u = u0 + ii[k] * du, v = vs[jj[k]], h = H[jj[k]][ii[k]];
                P[k][0] = tgx * u + dv.cnx * (D + v); P[k][1] = h; P[k][2] = tgz * u + dv.cnz * (D + v);
                col[0] += C[jj[k]][ii[k]][0] * 0.25f; col[1] += C[jj[k]][ii[k]][1] * 0.25f; col[2] += C[jj[k]][ii[k]][2] * 0.25f;
                // smooth normal from the central differences at this corner, rotated into the scene
                int a0 = ii[k] > 0 ? ii[k]-1 : ii[k], a1 = ii[k] < NU-1 ? ii[k]+1 : ii[k], b0 = jj[k] > 0 ? jj[k]-1 : jj[k], b1 = jj[k] < nv-1 ? jj[k]+1 : jj[k];
                float su = (H[jj[k]][a1] - H[jj[k]][a0]) / ((a1 - a0) * du), sv = (H[b1][ii[k]] - H[b0][ii[k]]) / maxf(vs[b1] - vs[b0], 1e-3f);
                nx += -(su * tgx + sv * dv.cnx); ny += 1.f; nz += -(su * tgz + sv * dv.cnz);
            }
            float nl = sqrtf(nx*nx + ny*ny + nz*nz); mb_set_normal(&m, nx / nl, ny / nl, nz / nl, 1.f);
            // winding: the quad must face up whichever way the coast frame is handed
            float ex = P[1][0] - P[0][0], ez = P[1][2] - P[0][2], fx = P[3][0] - P[0][0], fz = P[3][2] - P[0][2];
            if (ex * fz - ez * fx < 0) mb_quad(&m, P[0], P[1], P[2], P[3], col[0], col[1], col[2], 0);
            else mb_quad(&m, P[0], P[3], P[2], P[1], col[0], col[1], col[2], 0);
        }
        mb_auto_normal(&m);
        unsigned seed = 7 + (unsigned)dv.land * 31u;
        stage_trees(&m, &seed);
    }
    dv.stage = mesh_upload(&m);
    dv.stage_built = 1;
}

void DetailView::stage_city(void) {
    mb_t m = {0};
    float S = 700;
    mb_material(&m, MB_STONE);
    mb_quad(&m, v3f_t{-S,0,S}.v, v3f_t{S,0,S}.v, v3f_t{S,0,-120}.v, v3f_t{-S,0,-120}.v, 0.38f,0.50f,0.28f, 0);     // grass
    mb_quad(&m, v3f_t{-S,0.02f,-120}.v, v3f_t{S,0.02f,-120}.v, v3f_t{S,0.02f,-160}.v, v3f_t{-S,0.02f,-160}.v, 0.80f,0.74f,0.55f, 0);  // beach
    mb_material(&m, MB_STONE);
    mb_quad(&m, v3f_t{-90,0.05f,90}.v, v3f_t{90,0.05f,90}.v, v3f_t{90,0.05f,-90}.v, v3f_t{-90,0.05f,-90}.v, 0.55f,0.52f,0.47f, 0);    // cobbled town ground
    dv.stage = mesh_upload(&m);
    dv.stage_built = 1;
}

// ---------------------------------------------------------------------------
// battle scene: one entry per ship, each with its own course, speed and gun timer

// Every ship sails by her class's numbers (shipyard_class_stats): a carrack makes five knots
// with a fair wind and turns at three degrees a second, a fusta under oars is half as fast
// again and twice as handy. In a fight nobody carries full sail, so each ship is given 55-90 %
// of her best speed and keeps it.
void DetailView::battle_add_ship(int type, int side, float x, float z, float heading, unsigned col, unsigned *seed, int nation, int src_fleet, int src_slot) {
    if (dv.nships >= DV_MAX_SHIPS) return;
    dv_ship_t *s = &dv.ship[dv.nships++];
    ship_stats_t st; shipyard_class_stats(type, nation, &st);
    float best = maxf(st.knots, st.oar_knots);
    float speed = best * DV_KNOT * 0.95f;    // her cap; the line settles on the slowest ship's pace
    (void)rnd_f(seed);                       // keep the stream so the rest of the scene is unchanged
    *s = dv_ship_t{ type, side, x, z, heading, speed, rnd_f(seed) * 6.28f,
                      col_r(col), col_g(col), col_b(col), 3.f + rnd_f(seed) * 6.f, nation,
                      heading, st.turn_dps * 0.0174533f, st.length_m / DV_M_PER_UNIT, 0,
                      st.guns, st.oar_knots > 0.5f * st.knots, -1,
                      st.beam_m / DV_M_PER_UNIT, st.tonnes,
                      st.oar_knots * DV_KNOT, 0.f, src_fleet, src_slot, 0.f, { -1.f, -1.f, -1.f, -1.f } };
    // Her rigid body (0.22.9, P1.3): the battle is stepped at a fixed rate and the hulls
    // collide, so from here on the formation code only *asks* for a course and a speed.
    if (dv.mode == DV_BATTLE || dv.want_mode == DV_BATTLE)
        s->body = nphys_add(x, z, heading, s->len, s->beam, 0.42f * s->beam, s->tonnes);
}

// The battle line (fleet_sails battles are fought in formation, not as a swarm): each side
// sails a stadium circuit - two straight legs joined by half-turn caps - in line ahead. Every
// ship holds the station her slot gives her, so the column keeps its spacing, and because the
// stations round the caps one after another, the line "turns in succession" exactly where the
// leader turned, the way a seventeenth-century line of battle came about. The two circuits
// mirror each other: the inner legs run parallel and opposed DV_LINE_SEP apart - inside gun
// range, outside ramming distance - so the lines close, hold the range on the pass, trade
// broadsides, wear round the cap and come back.
#define DV_LINE_LEG   150.f   // straight leg length
#define DV_LINE_R      26.f   // half-turn radius (a great carrack at line speed needs ~20)
#define DV_LINE_SEP    28.f   // between the two inner legs: a cable or so, inside the 55-unit gun range
#define DV_LINE_GAP    26.f   // along-line spacing (the biggest hull is ~19 local units)
#define DV_LINE_AHEAD   9.f   // the pursuit point sails this far ahead of the station

// the straight leg grows with the longer line (0.22.10: 44-ship trailers), so the biggest hull
// still has a gap of its own length and the lines do not start the fight already rafted up
static float g_line_leg = DV_LINE_LEG;
float DetailView::dv_line_perim(void) { return 2.f * g_line_leg + 2.f * 3.14159265f * DV_LINE_R; }

void DetailView::dv_line_point(int side, float sarc, float *x, float *z, float *hdg) {
    float P = dv_line_perim();
    sarc = fmodf(sarc, P); if (sarc < 0) sarc += P;
    float zc = -(DV_LINE_SEP * 0.5f + DV_LINE_R);            // side 0 circles the southern stadium
    float X, Z, H;
    const float DV_PI = 3.14159265f;
    const float LEG = g_line_leg;
    if (sarc < LEG) {                                        // inner leg, east
        X = -LEG * 0.5f + sarc; Z = zc + DV_LINE_R; H = 0.f;
    } else if (sarc < LEG + DV_PI * DV_LINE_R) {                // east cap: port round, heading 0 -> pi
        float a = (sarc - LEG) / DV_LINE_R;
        X = LEG * 0.5f + DV_LINE_R * sinf(a); Z = zc + DV_LINE_R * cosf(a); H = a;
    } else if (sarc < 2.f * LEG + DV_PI * DV_LINE_R) {          // outer leg, west
        float t = sarc - LEG - DV_PI * DV_LINE_R;
        X = LEG * 0.5f - t; Z = zc - DV_LINE_R; H = DV_PI;
    } else {                                                 // west cap, back onto the inner leg
        float a = (sarc - 2.f * LEG - DV_PI * DV_LINE_R) / DV_LINE_R;
        float h2 = DV_PI + a;
        X = -LEG * 0.5f + DV_LINE_R * sinf(h2); Z = zc + DV_LINE_R * cosf(h2); H = h2;
    }
    if (side) { X = -X; Z = -Z; H += 3.14159265f; }          // side 1: the mirrored northern circuit
    *x = X; *z = Z; *hdg = H;
}

// After both fleets are in: give every ship her place in her side's line, set the line's pace
// to its slowest sail, and start everyone *on station* - a battle is met formed, not forming.
void DetailView::battle_form_lines(void) {
    dv.nside[0] = dv.nside[1] = 0; dv.line_s[0] = dv.line_s[1] = 0.f;
    float vmin[2] = { 1e9f, 1e9f };
    float lmax = 0.f;
    for (int i = 0; i < dv.nships; ++i) { dv_ship_t *sh = &dv.ship[i];
        sh->slot = dv.nside[sh->side]++;
        vmin[sh->side] = minf(vmin[sh->side], sh->speed); lmax = maxf(lmax, sh->len); }
    {   // the leg a line of this many hulls needs: every ship her own length and a third clear
        int nmax = maxi(dv.nside[0], dv.nside[1]);
        float want = ((float)nmax * maxf(DV_LINE_GAP, lmax * 1.35f) + 12.f - 2.f * 3.14159265f * DV_LINE_R) * 0.5f;
        g_line_leg = maxf(DV_LINE_LEG, want); }
    float P = dv_line_perim();
    for (int k = 0; k < 2; ++k) {
        dv.line_v[k]   = vmin[k] < 1e8f ? vmin[k] * 0.8f : 0.f;
        dv.line_gap[k] = dv.nside[k] > 0 ? minf(DV_LINE_GAP, (P - 12.f) / (float)dv.nside[k]) : DV_LINE_GAP;
    }
    // 0.22.10: a battle is *joined*, not approaching. The leaders used to start at the far ends
    // of the inner legs and needed four minutes to meet; they start abreast of the scene centre
    // now, the two lines already passing each other along the whole inner leg.
    dv.line_s[0] = dv.line_s[1] = 0.5f * g_line_leg;
    for (int i = 0; i < dv.nships; ++i) { dv_ship_t *sh = &dv.ship[i];
        dv_line_point(sh->side, dv.line_s[sh->side] - sh->slot * dv.line_gap[sh->side], &sh->x, &sh->z, &sh->heading);
        sh->course = sh->heading;
        if (sh->body >= 0) nphys_teleport(sh->body, sh->x, sh->z, sh->heading); }
}

void DetailView::build_battle(int fleet_id) {
    dv.nships = 0; dv.time = 0; dv.hits = 0;
    particles_reset();
    nphys_reset();
    unsigned seed = 1234;
    // find the battle: the chosen fleet, else the freshest firing fleet
    const fleet_t *hot = fleet_by_id(fleet_id);
    if (!hot || !hot->alive) { hot = 0;
        for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i];
            if (f->alive && f->at_port < 0 && f->last_fire > G.day - 1.f && (!hot || f->last_fire > hot->last_fire)) hot = f; } }
    int real = 0;
    if (hot) {   // everyone in the fight: fleets near the hot one that fired recently (both sides)
        for (int i = 0; i < G.nfleets && dv.nships < DV_MAX_SHIPS; ++i) { const fleet_t *f = &G.fleets[i];
            if (!f->alive || f->at_port >= 0 || dist2d(f->x, f->z, hot->x, hot->z) > 6.f * WORLD_SCALE) continue;
            if (f->last_fire < G.day - 1.f && f->id != hot->id) continue;
            int side = f->nation == hot->nation ? 0 : 1; real = 1;
            unsigned col = f->role == ROLE_PIRATE ? rgb(25,25,25) : nations_info[f->nation].color;
            for (int k = 0; k < f->nships; ++k) {
                float lx = -46 + (dv.nships % 7) * 15.f + rnd_f(&seed) * 7, lz = (side ? 14 : -14) + ((dv.nships / 7) % 3) * 19.f + rnd_f(&seed) * 8;
                battle_add_ship(f->ships[k].type, side, lx, lz, side ? 3.14159f + rnd_f(&seed) * 0.5f : rnd_f(&seed) * 0.5f, col, &seed, f->nation, f->id, k);
                // 0.22.22 (todo 178): she comes into the action with the people and the powder
                // she actually has - a ship that fought yesterday is short of both
                if (dv.nships > 0 && dv.ship[dv.nships - 1].body >= 0)
                    nphys_man(dv.ship[dv.nships - 1].body, f->ships[k].crew, f->ships[k].ammo);
            }
        }
    }
    if (!real) {   // no battle under way: stage a sample exercise matching the reference framing
        int na = G.player >= 0 ? G.player : NATION_ENGLAND, nb = NATION_VENICE;   // the exercise shows both fleets: northern rigs against Mediterranean oars
        unsigned ca = nations_info[na].color, cb = rgb(140, 25, 25);
        int per = 15;   // MARE_BATTLE_SHIPS=N stages N ships (0.22.10, the trailer's 44); DV_MAX_SHIPS caps it
        { const char *e = getenv("MARE_BATTLE_SHIPS"); if (e && atoi(e) > 0) per = mini(maxi(atoi(e) / 2, 1), DV_MAX_SHIPS / 2); }
        for (int i = 0; i < per; ++i) {
            int ta = i % SHIP_COUNT, tb = (i * 2 + 1) % SHIP_COUNT;
            battle_add_ship(ta, 0, -56 + (i % 6) * 22 + rnd_f(&seed) * 8, -22 - (i / 6) * 20 + rnd_f(&seed) * 6, 0.25f + rnd_f(&seed) * 0.4f, ca, &seed, na, -1, -1);
            battle_add_ship(tb, 1, -48 + (i % 6) * 23 + rnd_f(&seed) * 8,  20 + (i / 6) * 19 + rnd_f(&seed) * 6, 3.0f + rnd_f(&seed) * 0.4f, cb, &seed, nb, -1, -1);
        }
        LOG_INFO("gfx", "detail view: no battle under way, staged a %d-ship exercise", dv.nships);
        { const char *e = getenv("MARE_BATTLE_FREEZE"); if (e && atoi(e)) { set_timescale(0.f);
              LOG_INFO("gfx", "battle frozen (MARE_BATTLE_FREEZE); ship 0 at %.1f %.1f hdg %.2f, ship 1 at %.1f %.1f",
                       dv.ship[0].x, dv.ship[0].z, dv.ship[0].heading, dv.nships > 1 ? dv.ship[1].x : 0.f, dv.nships > 1 ? dv.ship[1].z : 0.f); } }
    } else LOG_INFO("gfx", "detail view: battle with %d ships", dv.nships);
    battle_form_lines();
    LOG_INFO("gfx", "battle lines formed: %d against %d, line speeds %.2f / %.2f u/s, gaps %.0f / %.0f",
             dv.nside[0], dv.nside[1], dv.line_v[0], dv.line_v[1], dv.line_gap[0], dv.line_gap[1]);
    stage_battle();
}

void DetailView::helm(float d) {
    if (dv.mode != DV_BATTLE) return;
    if (!helm_on_) {                       // the first touch takes the course she already steers
        helm_side_ = 0;                    // the player's side leads the near line
        float hx, hz, hh; dv_line_point(helm_side_, dv.line_s[helm_side_], &hx, &hz, &hh);
        helm_course_ = hh; helm_on_ = 1;
        LOG_INFO("gfx", "battle: you have the helm (Q and E steer, H returns to the line)");
    }
    helm_course_ = wrap_angle(helm_course_ + d);
}
void DetailView::helm_release(void) {
    if (helm_on_) LOG_INFO("gfx", "battle: back into the line");
    helm_on_ = 0;
}


// The butcher's bill goes home (0.22.19, todo 153 / P1.3 "tactical results update strategic
// ship state"). Until now the damage model was scenery: a ship could be dismasted, holed and
// sunk in front of the player and sail away from the scene without a scratch, because nothing
// read `nphys_damage_t`. Now every tactical hull that stands for a real ship writes what
// happened to her back into `ship_t`.
//
// Three rules, and they matter:
//
//  * **Damage is taken, never given back.** The strategic ship keeps the worse of what she
//    had and what the battle did, so leaving and re-entering a scene cannot heal her, and the
//    discrete combat model (sim/combat.cc) going on in parallel cannot be undone by this.
//  * **Hull points follow the tactical hull damage**, down to a floor of a tenth, rather than
//    being set from it: the two models measure different things and the discrete one is still
//    the authority on whether a ship is lost in an unwatched action.
//  * **A ship that goes under in the scene is struck off the fleet.** That is the whole point
//    - a battle has to be able to cost you a ship you were watching.
//
// It runs on a timer during the action as well as on the way out, so a battle the player
// leaves by Escape, by the menu or by closing the game still counts.
void DetailView::write_back(void) {
    if (dv.mode != DV_BATTLE) return;
    int touched = 0, sunk = 0;
    for (int i = 0; i < dv.nships; ++i) {
        dv_ship_t *s = &dv.ship[i];
        if (s->src_fleet < 0 || s->body < 0) continue;          // a staged exercise owes nobody anything
        fleet_t *f = fleet_by_id(s->src_fleet);
        if (!f || !f->alive || s->src_slot < 0 || s->src_slot >= f->nships) continue;
        nphys_damage_t d; nphys_damage_get(s->body, &d);
        ship_t *sh = &f->ships[s->src_slot];
        if (d.sunk) {
            // struck off: the slot the others stand in must not shift under them, so she is
            // marked spent here and the fleet is compacted once, below
            sh->hull = 0.f; sh->d_flood = 1.f; sunk++; touched++;
            continue;
        }
        // 0.22.22 (todo 178): her butcher's bill and her expenditure go home too - casualties
        // are not replaced at sea and powder burnt is powder gone
        if (d.crew < sh->crew) { sh->crew = d.crew; touched++; }
        if (d.ammo < sh->ammo) { sh->ammo = d.ammo; touched++; }
        if (d.rig   > sh->d_rig)   { sh->d_rig   = d.rig;   touched++; }
        if (d.guns  > sh->d_guns)  { sh->d_guns  = d.guns;  touched++; }
        if (d.flood > sh->d_flood) { sh->d_flood = d.flood; touched++; }
        float want = ship_types[sh->type].hull * maxf(1.f - d.hull, 0.10f);
        if (want < sh->hull) { sh->hull = want; touched++; }
    }
    if (sunk) {
        for (int i = 0; i < G.nfleets; ++i) {
            fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
            for (int k = 0; k < f->nships; ) {
                if (f->ships[k].hull > 0.f) { k++; continue; }
                G.nations[f->nation].ships_lost++;
                f->ships[k] = f->ships[--f->nships];
                f->morale = maxf(f->morale - MORALE_LOSS_SHIP, 0.f);
            }
            if (f->nships == 0) f->alive = 0;
        }
        // the tactical bodies now point at slots that have moved: stop writing back for them
        for (int i = 0; i < dv.nships; ++i) dv.ship[i].src_fleet = -1;
    }
    if (touched) LOG_INFO("sim", "battle result written home: %d ship(s) carry their damage out of the scene, %d sunk", touched, sunk);
}

void DetailView::battle_update(float dt) {
    dv.time += dt;
    unsigned seed = (unsigned)(dv.time * 977) + 13;
    // The lines sail on - at the pace the wind gives the leader's heading (0.22.7, "wind speed
    // at sea battles"): a line beating up to windward crawls, one running down comes on fast.
    // A side mostly under oars ignores it.
    for (int k = 0; k < 2; ++k) {
        float wf = 1.f;
        if (dv.mode == DV_BATTLE && dv.nside[k] > 0) {
            int oars = 0; for (int i = 0; i < dv.nships; ++i) if (dv.ship[i].side == k && dv.ship[i].oared) oars++;
            if (oars * 2 < dv.nside[k]) { float lx, lz, lh; dv_line_point(k, dv.line_s[k], &lx, &lz, &lh);
                wf = lerpf(1.f, wind_factor(cosf(lh), -sinf(lh), dv.wdx, dv.wdz, dv.wstr), 0.7f); }
        }
        dv.line_s[k] += dv.line_v[k] * wf * dt;
    }
    for (int i = 0; i < dv.nships; ++i) {
        dv_ship_t *s = &dv.ship[i];
        if (dv.mode == DV_BATTLE) {
            // line ahead: chase a point just ahead of the station, never turning faster than
            // the class can, and trim speed to hold the spacing - astern of station crowds on
            // sail, ahead of it she luffs up and lets the line catch her
            float sarc = dv.line_s[s->side] - s->slot * dv.line_gap[s->side];
            float px, pz, ph; dv_line_point(s->side, sarc + DV_LINE_AHEAD, &px, &pz, &ph);
            float want = atan2f(-(pz - s->z), px - s->x);
            // 0.22.17 (issue14): under the helm her side steers the ordered course instead of
            // chasing its station, and holds the line's pace - nothing is pulling her round.
            int helmed = helm_on_ && s->side == helm_side_;
            if (helmed) want = helm_course_;
            // 0.22.19 (issue15): the player's orders, which override the station but not the
            // physics - she still cannot turn faster than her class, and a wounded rig still
            // costs her the speed to do it with.
            int ordered = s->side == helm_side_ && move_order_ != DV_MOVE_LINE, ord_press = 0;
            if (ordered) {
                float tx = 0, tz = 0; int found = 0; float best = 1e9f;
                float ex = 0, ez = 0; int nen = 0;
                for (int j = 0; j < dv.nships; ++j) {
                    const dv_ship_t *o = &dv.ship[j];
                    if (o->side == s->side || (o->body >= 0 && nphys_sunk(o->body))) continue;
                    ex += o->x; ez += o->z; nen++;
                    float d = dist2d(s->x, s->z, o->x, o->z);
                    if (d < best) { best = d; tx = o->x; tz = o->z; found = 1; }
                }
                if (nen) { ex /= nen; ez /= nen; }
                if (move_order_ == DV_MOVE_BOARD && found) {
                    want = atan2f(-(tz - s->z), tx - s->x);        // lay her aboard
                    ord_press = 1;                                  // and crowd on everything she has
                } else if (move_order_ == DV_MOVE_RETREAT && nen) {
                    want = atan2f(-(s->z - ez), s->x - ex);         // the enemy's centre, astern
                    ord_press = 1;
                } else if (move_order_ == DV_MOVE_LAST_STAND) {
                    // hold what she has: the station course, and no hauling off
                }
            }
            float dh = wrap_angle(want - s->heading);
            float stx, stz, sth; dv_line_point(s->side, sarc, &stx, &stz, &sth);
            float err = (stx - s->x) * cosf(sth) + (stz - s->z) * -sinf(sth);   // along-track: + = station ahead
            float v = helmed ? minf(dv.line_v[s->side], s->speed)
                             : minf(dv.line_v[s->side] * clampf(1.f + err / 22.f, 0.6f, 1.4f), s->speed);
            if (ord_press) v = s->speed;     // boarding or running: everything she will carry
            // 0.22.15 (issue12 "wind direction and sail direction for the speed"; "sails only and
            // the wind against her: move her backwards; with oars, oar speed and roll the sails
            // down"): every ship's own heading against the wind sets what her canvas gives
            // (wind_factor, the point-of-sail curve). In the eye of the wind a sailing ship's
            // canvas is aback and she makes sternway at a quarter of her speed; an oared ship
            // takes in her sails (furl -> 1, the shipyard draws no canvas) and pulls at her oar
            // speed instead, letting them fall again once the wind draws abaft the beam.
            {
                float hx = cosf(s->heading), hz = -sinf(s->heading);
                float wf = wind_factor(hx, hz, dv.wdx, dv.wdz, dv.wstr);
                float into = -(hx * dv.wdx + hz * dv.wdz);                 // 1 = dead into the wind
                int in_eye = into > 0.72f && dv.wstr > 0.25f;
                float want_furl = 0.f;
                // 0.22.20 (issue16 "add posiblity to retract sails if upwind"): the player's
                // order takes everything in. Under bare poles an oared ship pulls at her oar
                // speed; a pure sailing ship is hove to - a quarter knot of leeway, no drive -
                // which is precisely what taking in sail upwind is *for*.
                if (furl_order_ && s->side == helm_side_) {
                    want_furl = 1.f;
                    v = s->oar_speed > 0.05f ? minf(v, s->oar_speed) : 0.f;
                }
                else if (s->oar_speed > 0.05f && (in_eye || wf < 0.45f)) { want_furl = 1.f; v = minf(v, s->oar_speed); }   // under oars, canvas in
                else if (in_eye) v = -0.25f * s->speed * clampf((into - 0.72f) / 0.28f, 0.f, 1.f) * clampf(dv.wstr, 0.3f, 1.2f);   // aback: sternway
                else v *= clampf(wf / 1.0f, 0.3f, 1.3f);
                s->furl += clampf(want_furl - s->furl, -dt * 0.5f, dt * 0.5f);   // sails come in or are set over two seconds
            }
            // 0.22.9 (P1.3): the helm, not the position. The body is integrated below at the
            // fixed tactical sub-step and may be shouldered off station by a collision or
            // brought up all standing by the ground; the formation only ever asks.
            if (s->body >= 0) v *= nphys_speed_factor(s->body);   // 0.22.17 (P1.3): a wounded rig and water in her
            if (s->body >= 0) nphys_drive(s->body, v, want, s->turn);
            else { s->heading += clampf(dh, -s->turn * dt, s->turn * dt); s->x += cosf(s->heading) * v * dt; s->z -= sinf(s->heading) * v * dt; }
        } else {
            // the city's moored ships keep their gentle wander off the piers
            float want = s->course + 0.22f * sinf(dv.time * 0.09f + s->phase);
            float d = sqrtf(s->x * s->x + s->z * s->z);
            if (d > 70) { s->course = atan2f(-(0 - s->z), 0 - s->x); want = s->course; }
            float dh = wrap_angle(want - s->heading);
            s->heading += clampf(dh, -s->turn * dt, s->turn * dt);
            s->x += cosf(s->heading) * s->speed * dt;
            s->z -= sinf(s->heading) * s->speed * dt;
        }
        if (s->body >= 0 && nphys_sunk(s->body)) continue;   // 0.22.17: a wreck neither steers nor fires
        // 0.22.19 (todo 152): a ship alight smokes from where she is burning, and the fire
        // grows visibly. The volumetric system is already here - this is one more source.
        if (s->body >= 0) {
            nphys_damage_t fd; nphys_damage_get(s->body, &fd);
            if (fd.fire > 0.02f) {
                s->fire_puff -= dt;
                if (s->fire_puff < 0.f) {
                    s->fire_puff = 0.26f - 0.17f * fd.fire;          // the harder she burns, the thicker it comes (0.22.22: twice the rate, issue18 "add more smoke particles by much")
                    particles_fire(dv.cx + s->x, 0.06f * s->len, dv.cz + s->z, s->len, fd.fire);
                }
            }
        }
        for (int k = 0; k < 4; ++k) if (s->mast_fall[k] >= 0.f) s->mast_fall[k] += dt;   // 0.22.23 (issue19): the mast going by the board
        s->next_fire -= dt;
        if (s->next_fire < 0) {                                                // broadside at the nearest enemy within range
            // 0.22.19 (todo 149, "gun damage with teeth"): the battery scalar has been kept
            // since 0.22.17 and done nothing. A ship whose guns have been shot off their
            // carriages fires slower, because she has fewer of them and fewer men to serve
            // them - up to four times slower - and past nine tenths she has no battery left
            // to fire at all.
            float gd = 0.f;
            if (s->body >= 0) { nphys_damage_t d2; nphys_damage_get(s->body, &d2); gd = d2.guns; }
            s->next_fire = (4.f + rnd_f(&seed) * 7.f) * (1.f + 3.0f * gd);
            if (gd > 0.90f) continue;                                           // her guns are wreckage
            // 0.22.19 (issue15 "direct fire to warship ... or tradeships"): the guns choose
            // their target by the player's standing order. Only the player's side obeys it;
            // the enemy always engages the nearest, as before. "Heaviest" and "lightest" are
            // by the number of guns she carries, which is what tells a man-of-war from a
            // merchantman at a cable's distance.
            int pick = -1; float pick_score = 0.f;
            for (int j = 0; j < dv.nships; ++j) {
                if (dv.ship[j].side == s->side) continue;
                if (dv.ship[j].body >= 0 && nphys_sunk(dv.ship[j].body)) continue;
                float d = dist2d(s->x, s->z, dv.ship[j].x, dv.ship[j].z);
                if (d > 55.f) continue;
                int order = s->side == helm_side_ ? fire_order_ : DV_FIRE_NEAREST;
                float score;
                if (order == DV_FIRE_WARSHIPS)      score = (float)dv.ship[j].guns - d * 0.25f;
                else if (order == DV_FIRE_TRADERS)  score = -(float)dv.ship[j].guns - d * 0.25f;
                else                                score = -d;
                if (pick < 0 || score > pick_score) { pick = j; pick_score = score; }
            }
            if (pick >= 0) {
                const int j = pick;
                // the broadside facing the target: flashes, smoke and embers from every gun on
                // that side, sized by the class (particles.h); the smoke drifts off on the wind
                float fx = cosf(s->heading), fz = -sinf(s->heading), tx = dv.ship[j].x - s->x, tz = dv.ship[j].z - s->z;
                float side = (-fz * tx + fx * tz) >= 0.f ? 1.f : -1.f;
                int serve = maxi((int)(s->guns * (1.f - gd) + 0.5f), 1);     // the pieces she can still serve
                // 0.22.22 (todo 178): ... and only as many as her people can man and her
                // lockers can load. A ship that has shot herself dry falls silent, which is
                // what makes a long action end.
                if (s->body >= 0) serve = nphys_spend_ammo(s->body, serve);
                if (serve <= 0) continue;
                particles_broadside(dv.cx + s->x, 0.f, dv.cz + s->z, s->heading, side, s->len, maxi(serve, 2), s->body);
            }
        }
    }
    // One fixed-step advance for the whole tactical world, then the poses the renderer draws
    // (interpolated by the leftover accumulator, so the hulls are smooth at any frame rate).
    if (dv.mode == DV_BATTLE) {
        if (dv.coast_on)
            nphys_set_coast(dv.cnx * dv.coast_dist, dv.cnz * dv.coast_dist,
                            dv.cnx, dv.cnz, DV_SHORE_AMP, DV_SHORE_FREQ, DV_SHORE_SLOPE, 1);
        else nphys_set_coast(0, 0, 0, -1, 0, 0.01f, 0.15f, 0);
        // the water the hulls ride (0.22.11, P1.3 buoyancy): the same wave sum the sea draws,
        // in the staged scene's stretched units; the bodies are in local coordinates
        nphys_set_surface(dv_surface, this);
        // 0.22.23: what the tactical world actually costs, so "physics" work is aimed at a
        // measurement and not a guess (issue19 asks for the physics items next)
        { double t0 = diag_now(); nphys_advance(dt);
          static double acc = 0; static int nf = 0; acc += (diag_now() - t0) * 1000.0; nf++;
          if (nf >= 120) { LOG_INFO("sim", "physics: %.3f ms a frame over %d frames (%d bodies, %d steps, %d contacts, %d shot)",
                                    acc / nf, nf, nphys_count(), nphys_steps_last(), nphys_contacts_last(), nphys_shots());
                           acc = 0; nf = 0; } }
        for (int i = 0; i < dv.nships; ++i) { dv_ship_t *s = &dv.ship[i];
            if (s->body < 0) continue;
            nphys_pose(s->body, &s->x, &s->z, &s->heading); }
        // what the shot did (ballistics, 0.22.11): a splash where a ball went into the sea, a
        // burst of splinters and dust where one went into a hull
        nphys_event_t ev[64]; int nev = nphys_poll_events(ev, 64);
        {   // 0.22.17 (P1.3): the butcher's bill, every ten seconds of battle, so a run can be read
        static float dmg_log = 0.f; dmg_log += dt;
        { static float wb_t = 0.f; wb_t += dt; if (wb_t > 5.f) { wb_t = 0.f; write_back(); } }   // 0.22.19 (todo 153): and home it goes
        if (dmg_log > 10.f) { dmg_log = 0.f;
            float wh = 0, wr = 0, wf = 0, lc = 1.f, la = 1.f; int nsunk = 0, nmast = 0;
            for (int i = 0; i < dv.nships; ++i) { if (dv.ship[i].body < 0) continue;
                nphys_damage_t d; nphys_damage_get(dv.ship[i].body, &d);
                if (d.hull > wh) wh = d.hull; if (d.rig > wr) wr = d.rig; if (d.flood > wf) wf = d.flood;
                if (d.crew < lc) lc = d.crew; if (d.ammo < la) la = d.ammo;   // 0.22.22 (todo 178)
                nsunk += d.sunk; nmast += d.masts_down; }
            int nbody = 0; for (int i = 0; i < dv.nships; ++i) nbody += dv.ship[i].body >= 0;
            LOG_INFO("sim", "battle damage: %d hits, worst hull %.2f rig %.2f flood %.2f, thinnest crew %.2f driest lockers %.2f, %d mast(s) down, %d sunk; %d bodies, %d shot in the air",
                     dv.hits, wh, wr, wf, lc, la, nmast, nsunk, nbody, nphys_shots()); }
    }
    for (int e = 0; e < nev; ++e) {
            float sz = 0.7f + 0.004f * ev[e].speed;
            if (ev[e].kind == NPHYS_EV_SPLASH) particles_splash(dv.cx + ev[e].x, dv.cz + ev[e].z, sz);
            else if (ev[e].kind == NPHYS_EV_MAST_DOWN) {   // 0.22.17: a mast carried away
                particles_hit(dv.cx + ev[e].x, ev[e].y, dv.cz + ev[e].z, 0.f, 1.f, 2.2f);
                // 0.22.23 (issue19): start this station's fall, so the shipyard can draw it
                // going over the side instead of blinking out
                for (int i = 0; i < dv.nships; ++i) if (dv.ship[i].body == ev[e].body) {
                    nphys_damage_t d; nphys_damage_get(ev[e].body, &d);
                    int k = d.masts_down - 1;
                    if (k >= 0 && k < 4 && dv.ship[i].mast_fall[k] < 0.f) dv.ship[i].mast_fall[k] = 0.f;
                }
                LOG_INFO("gfx", "battle: a mast goes by the board");
            }
            else if (ev[e].kind == NPHYS_EV_FIRE) {        // 0.22.19 (todo 152)
                LOG_INFO("gfx", "battle: she is on fire");
            }
            else if (ev[e].kind == NPHYS_EV_STRANDED) {    // 0.22.20 (issue16)
                particles_splash(dv.cx + ev[e].x, dv.cz + ev[e].z, 2.2f);
                LOG_INFO("gfx", "battle: she has taken the ground and is stuck fast");
            }
            else if (ev[e].kind == NPHYS_EV_EXPLODE) {
                float L = 20.f;
                for (int i = 0; i < dv.nships; ++i) if (dv.ship[i].body == ev[e].body) L = dv.ship[i].len;
                particles_explosion(dv.cx + ev[e].x, ev[e].y, dv.cz + ev[e].z, L);
                LOG_INFO("gfx", "battle: her magazine has gone");
            }
            else if (ev[e].kind == NPHYS_EV_SUNK) {
                particles_splash(dv.cx + ev[e].x, dv.cz + ev[e].z, 3.4f);
                LOG_INFO("gfx", "battle: she has gone down");
            }
            else if (ev[e].body >= 0) {
                float bx = 0, bz = 0, bh = 0; nphys_pose(ev[e].body, &bx, &bz, &bh);
                float dx = ev[e].x - bx, dz = ev[e].z - bz, l = sqrtf(dx * dx + dz * dz); if (l > 1e-4f) { dx /= l; dz /= l; }
                particles_hit(dv.cx + ev[e].x, ev[e].y + 0.3f, dv.cz + ev[e].z, dx, dz, sz);
                dv.hits++;
            }
        }
        // weather in the battle (0.22.11, todo 19): in a fresh breeze the stems throw spray
        // that the wind carries over the decks
        // 0.22.14 (issue11 "add ship waves and splashing"): every stem under way throws spray as she
        // pitches into the sea, more the faster she goes and the fresher the wind; the breeze adds its own
        if (dt > 0.f) {
            for (int i = 0; i < dv.nships; ++i) { dv_ship_t *s = &dv.ship[i];
                if (s->body < 0) continue;
                float spd = nphys_speed(s->body); if (spd < 0.3f) continue;
                float rate = 0.9f * clampf(spd / 2.5f, 0.f, 1.2f) + 3.f * clampf(dv.wstr - 0.7f, 0.f, 1.f);
                if (rnd_f(&seed) > dt * rate) continue;
                float fx = cosf(s->heading), fz = -sinf(s->heading);
                particles_spray(dv.cx + s->x + fx * 0.46f * s->len, 0.10f * s->len, dv.cz + s->z + fz * 0.46f * s->len, dv.wdx, dv.wdz, clampf(0.35f + 0.5f * (dv.wstr - 0.6f) + 0.2f * spd / 2.5f, 0.25f, 1.f));
            }
        }
    }
    particles_update(dt);
}

// the staged water under a hull, for the rigid bodies (sim/naval_physics.h surface callback):
// local coordinates in, the wave sum the sea draws (scaled to the staged scene's units) out
float DetailView::dv_surface(float x, float z, void *self) {
    DetailView *d = (DetailView *)self;
    return sea_height_at((d->dv.cx + x) / DV_SHIP_SCALE, (d->dv.cz + z) / DV_SHIP_SCALE) * DV_SHIP_SCALE * DV_SEA_AMP;
}

// ---------------------------------------------------------------------------
// city scene: districts laid out from the port's size, population and buildings
// (harbour at the waterfront, commercial centre, residential mass, church, walls when a fort
//  exists, farms outside) - the reference layout of Designer (14) at 1:1

#define DV_HOUSE 3.2f       // base house scale: ~13 m footprint
asset_t *DetailView::dv_house_asset(float ring, int size, unsigned *seed) {
    const char *const *roles; int n;
    float tier = ring + (rnd_f(seed) - 0.5f) * 0.35f;
    if (tier < 0.3f && size >= 2) { roles = dv_roles_rich; n = 4; } else if (tier < 0.7f) { roles = dv_roles_mid; n = 6; } else { roles = dv_roles_poor; n = 3; }
    int start = rnd_int(seed, n);
    for (int k = 0; k < n; ++k) { asset_t *a = asset_get_role(ASSET_BUILDING, roles[(start + k) % n]); if (a) return a; }
    return NULL;
}
int DetailView::dv_free(float x, float z, float r) {
    for (int i = 0; i < dv_nfp; ++i) if (dist2d(dv_fp[i][0], dv_fp[i][1], x, z) < dv_fp[i][2] + r) return 0;
    return 1;
}
void DetailView::dv_take(float x, float z, float r) { if (dv_nfp < 220) { dv_fp[dv_nfp][0]=x; dv_fp[dv_nfp][1]=z; dv_fp[dv_nfp][2]=r; dv_nfp++; } }

void DetailView::build_city(int port) {
    dv.nships = 0; dv_nfp = 0; particles_reset();
    const port_t *p = port >= 0 ? &G.ports[port] : 0;
    int size = p ? p->size : 2;
    float pop = p ? p->pop : 20;
    int whs = p ? 1 + p->building[B_WAREHOUSE] : 1, docks = p ? 1 + p->building[B_DOCKS] : 1;
    int market = p ? p->building[B_MARKET] : 1, fort = p ? p->building[B_FORT] : 1;
    unsigned col = p && p->size > 0 ? nations_info[p->nation].color : 0xFFBBBBBB;
    unsigned seed = p ? 77 + (unsigned)(p - G.ports) * 131 : 77;
    instlist_clear(&dv.houses); instlist_clear(&dv.roofs); instlist_clear(&dv.towers);
    instlist_clear(&dv.walls); instlist_clear(&dv.piers); instlist_clear(&dv.flags);

    float R = 55 + 22 * size;                               // town radius (walls sit here)
    // civic centre: tower + banner on the plaza
    *instlist_push(&dv.towers) = inst_t{ 6, 0, -8, 0.3f, 4.f + size, 0.8f,0.78f,0.72f, 0,0,0,0 };  dv_take(6, -8, 6);
    *instlist_push(&dv.flags)  = inst_t{ -6, 0, 2, 0, 8.f + size, col_r(col), col_g(col), col_b(col), 0,0,0,0 }; dv_take(-6, 2, 3);
    // (the church - the religious district - is placed at draw time from the shared asset mesh)
    dv_take(34, -14, 14);                                      // reserve the church plot east of the plaza
    // harbour district: piers into the water, warehouses on the front
    for (int i = 0; i < docks + 1; ++i) {
        float px = -60 + i * 38.f + rnd_f(&seed) * 8;
        *instlist_push(&dv.piers) = inst_t{ px, 0, -164, 1.5708f, 4.f, 1,1,1, 0,0,0, 12.f };
    }
    for (int i = 0; i < whs * 2 + 1; ++i) {
        float wx = -70 + i * 26.f + rnd_f(&seed) * 10, wz = -128 + rnd_f(&seed) * 10;
        float s = DV_HOUSE * 1.5f;
        if (!dv_free(wx, wz, s * 0.6f)) continue;
        *instlist_push(&dv.houses) = inst_t{ wx, 0, wz, 1.5708f, s, 0.60f,0.52f,0.42f, 0,0,0,0 };
        *instlist_push(&dv.roofs)  = inst_t{ wx, s, wz, 1.5708f, s, 0.35f,0.33f,0.30f, 0,0,0,0 };
        dv_take(wx, wz, s * 0.62f);
    }
    // commercial centre around the plaza (brighter plaster, denser) then residential mass.
    // Authored houses (same role tiers as the map's towns, scene.c) stand in for the boxes
    // where they have loaded, scaled from their world-map size to the 1:1 stage.
    int nres = mini(24 + (int)(pop * 1.4f) + 18 * market, 150);
    dv.nbuild = 0;
    for (int k = 0, placed = 0; k < nres * 5 && placed < nres; ++k) {
        float ring = sqrtf(rnd_f(&seed));
        float a = rnd_f(&seed) * 6.2832f, r = 14 + ring * (R - 22);
        float hx = cosf(a) * r, hz = sinf(a) * r * 0.82f - 20;
        if (hz < -108) continue;                              // keep out of the harbour water
        float s = DV_HOUSE * (0.8f + rnd_f(&seed) * 0.5f) * (r < 45 ? 1.25f : 1.f);
        asset_t *house = dv_house_asset(ring, size, &seed);
        float foot = house ? asset_footprint_radius(house) * (s / maxf(house->bmax[1] - house->bmin[1], 0.05f)) : s * 0.68f;
        if (!dv_free(hx, hz, foot)) continue;
        if (house && dv.nbuild < DV_MAX_BUILD) {
            float sc = s / maxf(house->bmax[1] - house->bmin[1], 0.05f);   // asset height -> this house's height
            dv.build[dv.nbuild++] = dv_build_t{ (short)(house - asset_at(0)), { hx, 0.02f, hz, -atan2f(-hz, -hx) + (rnd_f(&seed) - 0.5f) * 0.5f, sc, 1,1,1, 0,0,0,0 } };
        } else {
            float wc = 0.72f + rnd_f(&seed) * 0.2f;
            int commercial = r < 45 && market;
            *instlist_push(&dv.houses) = inst_t{ hx, 0, hz, rnd_f(&seed) * 3.14f, s, wc, wc * (commercial ? 0.95f : 0.9f), wc * 0.75f, 0,0,0,0 };
            *instlist_push(&dv.roofs)  = inst_t{ hx, s, hz, dv.houses.v[dv.houses.n-1].heading, s, 0.66f, 0.33f, 0.20f, 0,0,0,0 };
        }
        dv_take(hx, hz, foot * 1.03f);
        placed++;
    }
    // military district: the wall ring (when a fort exists) with corner towers
    if (fort > 0) {
        int nseg = 18 + 4 * fort;
        for (int k = 0; k < nseg; ++k) {
            float a0 = k * 6.2832f / nseg, a1 = (k + 1) * 6.2832f / nseg;
            float x0 = cosf(a0) * R, z0 = sinf(a0) * R * 0.82f - 20, x1 = cosf(a1) * R, z1 = sinf(a1) * R * 0.82f - 20;
            if ((z0 + z1) * 0.5f < -116) continue;            // the sea side stays open
            *instlist_push(&dv.walls) = inst_t{ (x0+x1)/2, 0, (z0+z1)/2, -atan2f(z1-z0, x1-x0), 2.6f + 0.5f * fort, 1,1,1, 0,0,0, dist2d(x0,z0,x1,z1) / (2.6f + 0.5f * fort) };
            if (k % 6 == 0) *instlist_push(&dv.towers) = inst_t{ x0, 0, z0, 0, 3.f + 0.5f * fort, 0.75f,0.73f,0.68f, 0,0,0,0 };
        }
    }
    // outer farms: huts scattered beyond the walls
    for (int i = 0; i < 6 + 3 * size; ++i) {
        float a = rnd_f(&seed) * 3.5f - 0.3f, r = R + 25 + rnd_f(&seed) * 90;
        float hx = cosf(a) * r, hz = sinf(a) * r * 0.82f - 20;
        if (hz < -100) continue;
        float s = DV_HOUSE * 0.7f;
        if (!dv_free(hx, hz, s)) continue;
        *instlist_push(&dv.houses) = inst_t{ hx, 0, hz, rnd_f(&seed) * 3.14f, s, 0.68f, 0.6f, 0.45f, 0,0,0,0 };
        *instlist_push(&dv.roofs)  = inst_t{ hx, s, hz, dv.houses.v[dv.houses.n-1].heading, s, 0.45f, 0.40f, 0.30f, 0,0,0,0 };
        dv_take(hx, hz, s);
    }
    // a couple of ships moored off the piers for life
    unsigned scol = col;
    battle_add_ship(SHIP_FLUYT, 0, -40, -180, 0.2f, scol, &seed, p ? p->nation : NATION_ENGLAND, -1, -1);
    battle_add_ship(SHIP_MERCHANT, 0, 25, -195, 2.9f, scol, &seed, p ? p->nation : NATION_ENGLAND, -1, -1);
    for (int i = 0; i < dv.nships; ++i) dv.ship[i].speed *= 0.25f;
    stage_city();
    LOG_INFO("gfx", "detail view: city '%s' size %d pop %.0f: %d houses, %d walls, fort %d, market %d",
             p ? p->name : "(sample)", size, pop, dv.houses.n, dv.walls.n, fort, market);
}

// ---------------------------------------------------------------------------
// transitions

void DetailView::dv_begin(int mode, int port, int fleet) {
    if (dv.phase == DVP_IDLE) { dv.saved_tx = cam.want_tx; dv.saved_tz = cam.want_tz; dv.saved_dist = cam.want_dist; dv.saved_yaw = cam.want_yaw; }
    dv.want_mode = mode; dv.want_port = port; dv.want_fleet = fleet;
    dv.phase = DVP_IN; dv.t = 0;
}
void DetailView::enter_battle(int fleet_id) {
    helm_on_ = 0; fire_order_ = DV_FIRE_NEAREST; move_order_ = DV_MOVE_LINE; furl_order_ = 0;   // 0.22.19: a new action, no standing orders
    dv_begin(DV_BATTLE, -1, fleet_id);
}
void DetailView::enter_city(int port) {
    if (port < 0) { float best = 1e9f;
        for (int i = 0; i < G.nports; ++i) { const port_t *pp = &G.ports[i];
            if (!port_is_gone(pp) && pp->size >= 1) { float d = dist2d(pp->x, pp->z, cam.tx, cam.tz); if (d < best) { best = d; port = i; } } } }
    if (port >= 0) dv_begin(DV_CITY, port, 0);
}
void DetailView::exit(void) { if (dv.phase == DVP_SHOW) { write_back(); LOG_INFO("gfx", "detail view: leaving (t=%.1f)", dv.t); dv.phase = DVP_OUT_IN; dv.t = 0; } }
// drop the scene at once, no clouds, no camera restore (the menu preview, 0.22.11)
void DetailView::abort(void) {
    write_back();      // 0.22.19 (todo 153): even a scene dropped in a hurry pays its bill
    dv.phase = DVP_IDLE; dv.mode = DV_OFF; stage_free(); wake_set_region(0, 0, 0); particles_reset(); nphys_reset();
    cam_pitch_scale = 1.f; cam.ty = cam.want_ty = 0.f;
}

float DetailView::dv_cover(void) {   // cloud cover 0..1 for the current phase
    switch (dv.phase) {
        case DVP_IN:     return dv.t / DV_CLOUD_IN;
        case DVP_SHOW:   return dv.t < DV_CLOUD_OUT ? 1.f - dv.t / DV_CLOUD_OUT : 0.f;
        case DVP_OUT_IN: return dv.t / DV_CLOUD_IN;
        case DVP_OUT:    return 1.f - dv.t / DV_CLOUD_OUT;
        default:         return 0;
    }
}

// The staged scenes are ~3.2x smaller per unit than the map, so the shared wave field is
// stretched by DV_SHIP_SCALE to keep its physical size relative to the ships, and the heightmap
// lookup is replaced by a constant depth (there is no world under a staged scene).
void DetailView::dv_sea_params(void) {
    int on = dv.phase != DVP_IDLE && dv.phase != DVP_OUT;
    sea_wave_scale = on ? DV_SHIP_SCALE : 1.f;
    sea_local_amp  = on ? DV_SEA_AMP : 1.f;   // a staged sea is stretched: cut the wave height to match
    sea_local_depth = on ? DV_SEA_DEPTH : 0.f;
    // the shore profile the water shares with the stage (0.22.7): the battle's probed coast,
    // the city's straight beach; off when the probe found open sea
    float shore_on = 0.f;
    if (on && dv.mode == DV_BATTLE && dv.coast_on) {
        sea_local_shore_a[0] = dv.cx + dv.cnx * dv.coast_dist; sea_local_shore_a[1] = dv.cz + dv.cnz * dv.coast_dist;
        sea_local_shore_a[2] = dv.cnx; sea_local_shore_a[3] = dv.cnz;
        sea_local_shore_b[0] = DV_SHORE_AMP; sea_local_shore_b[1] = DV_SHORE_FREQ; sea_local_shore_b[2] = DV_SHORE_SLOPE; shore_on = 1.f;
    } else if (on && dv.mode == DV_CITY) {
        sea_local_shore_a[0] = dv.cx; sea_local_shore_a[1] = dv.cz - 158.f; sea_local_shore_a[2] = 0.f; sea_local_shore_a[3] = 1.f;
        sea_local_shore_b[0] = 0.f; sea_local_shore_b[1] = 0.01f; sea_local_shore_b[2] = 0.12f; shore_on = 1.f;
    }
    sea_local_shore_b[3] = shore_on;
}
void DetailView::sea(void) {
    // The battle is open ocean; the city's water stops at its beach (stage_city lays the sand
    // out to local z = -160), so the staged sea is clipped there instead of flooding the town.
    sea_clip_on = dv.mode == DV_CITY;
    sea_clip_z = dv.cz - 158.f;
    sea_draw_local(DV_SEA_DEPTH, DV_SHIP_SCALE, 1600.f);
    sea_clip_on = 0;
}

void DetailView::frame(float dt) {
    dv_sea_params();
    if (dv.phase == DVP_IDLE) return;
    dv.t += dt;
    if (dv.phase == DVP_IN && dv.t >= DV_CLOUD_IN) {          // fully covered: swap to the staged scene
        dv.mode = dv.want_mode;
        // local space, kept far enough inside the world box that the camera clamp never bites
        // (the z span of the world is only ~520 units, so the margin must stay well under half)
        dv.cx = clampf(cam.want_tx, lon2x(LON0) + 140, lon2x(LON1) - 140);
        dv.cz = clampf(cam.want_tz, lat2z(LAT1) + 140, lat2z(LAT0) - 140);
        LOG_INFO("gfx", "detail view: scene centred at %.0f %.0f (cam was %.0f %.0f)", dv.cx, dv.cz, cam.want_tx, cam.want_tz);
        // the wind at the battle's position: the sea's wave set already follows it; now the
        // ships' pace, the smoke and the arrows do too (0.22.7)
        wind_at(dv.cx, dv.cz, G.day, &dv.wdx, &dv.wdz, &dv.wstr);
        dv.timescale = 1.f; dv.show_wind = getenv("MARE_RECORD") ? 0 : 1; dv.reprobe_t = 0.f;   // a film has no debug arrows in it (0.22.10)
        particles_set_wind(dv.wdx * dv.wstr * DV_WIND_KN * 0.5144f / DV_LOCAL_M, dv.wdz * dv.wstr * DV_WIND_KN * 0.5144f / DV_LOCAL_M);
        particles_set_origin(dv.cx, dv.cz);   // 0.22.17: the shot and the recoil belong to the scene's own frame
        if (dv.mode == DV_BATTLE) build_battle(dv.want_fleet); else build_city(dv.want_port);
        wake_set_region(dv.cx, dv.cz, 420.f);   // the staged scene never moves, so the map never scrolls
        cam.tx = cam.want_tx = dv.cx; cam.tz = cam.want_tz = dv.cz + (dv.mode == DV_BATTLE ? -25 : -30);
        cam.ty = cam.want_ty = 0.f;
        cam.dist = 380; cam.want_dist = dv.mode == DV_BATTLE ? 32 : 110;      // the dive through the clouds
        // battle: facing the coast (at its real bearing now), or north over the open sea
        cam.yaw = cam.want_yaw = dv.mode == DV_BATTLE ? (dv.coast_on ? atan2f(dv.cnx, -dv.cnz) : 0.f) + 0.12f : 0.25f;
        if (dv.mode == DV_BATTLE) { cam.tx = cam.want_tx = dv.cx - dv.cnx * 25.f; cam.tz = cam.want_tz = dv.cz - dv.cnz * 25.f; }
        cam_pitch_scale = 0.72f;                                               // towards the horizon, like the concept frames
        // MARE_DV_CAM="dist,yaw_deg[,x,z[,pitch]]" pins the staged camera for screenshots: local
        // offsets from the scene centre, so a run can frame one ship at a given distance
        { const char *e = getenv("MARE_DV_CAM");
          if (e) { float di = 32, ya = 7, ox = 0, oz = -25, ps = 0.72f; sscanf(e, "%f,%f,%f,%f,%f", &di, &ya, &ox, &oz, &ps);
                   cam.want_dist = cam.dist = di; cam.want_yaw = cam.yaw = ya * 0.0174533f;
                   cam.tx = cam.want_tx = dv.cx + ox; cam.tz = cam.want_tz = dv.cz + oz; cam_pitch_scale = ps;
                   LOG_INFO("gfx", "detail view camera pinned (MARE_DV_CAM): dist %.0f yaw %.0f at %.0f,%.0f", di, ya, ox, oz); } }
        dv.phase = DVP_SHOW; dv.t = 0;
    }
    if (dv.phase == DVP_OUT_IN && dv.t >= DV_CLOUD_IN) {      // covered again: restore the map camera
        cam_pitch_scale = 1.f;
        cam.ty = cam.want_ty = 0.f;
        cam.tx = cam.want_tx = dv.saved_tx; cam.tz = cam.want_tz = dv.saved_tz;
        cam.dist = cam.want_dist = dv.saved_dist; cam.yaw = cam.want_yaw = dv.saved_yaw;
        dv.phase = DVP_OUT; dv.t = 0;
    }
    if (dv.phase == DVP_OUT && dv.t >= DV_CLOUD_OUT) { dv.phase = DVP_IDLE; dv.mode = DV_OFF; stage_free(); wake_set_region(0, 0, 0); particles_reset(); return; }
    if (dv.phase != DVP_SHOW) return;

    // 0.22.25: the floor is relaxable for inspection. Six units is right for play, but it is
    // also why four rounds of oar screenshots were taken from too far to judge anything.
    { static float mind = -1.f; if (mind < 0.f) { const char *e = getenv("MARE_DV_MINDIST"); mind = e ? (float)atof(e) : 6.f; }
      cam.want_dist = clampf(cam.want_dist, mind, 260); }   // stay under the map morph in local space
    cam.want_tx = clampf(cam.want_tx, dv.cx - 120, dv.cx + 120);
    cam.want_tz = clampf(cam.want_tz, dv.cz - 120, dv.cz + 120);
    cam.want_ty = dv.mode == DV_BATTLE ? clampf(cam.want_ty, 0.f, 140.f) : 0.f;   // the look-at point may rise over the battle (R/F)
    if (dv.mode == DV_BATTLE && getenv("MARE_RECORD")) cam_min_eye_y = -1e9f;        // the trailer's low passes decide their own height

    if (dv.mode == DV_BATTLE) battle_update(dt * dv.timescale);   // the battle clock (0.22.7): slowed or stopped by the player
    else battle_update(dt * 0.35f);                            // moored ships barely stir
    // The coast probe ran the moment the clouds closed; if the terrain chunks were not resident
    // yet it classified the land from the base heightmap. Ask again while they page in (first
    // few seconds), and rebuild the stage if the answer changed.
    if (dv.mode == DV_BATTLE && dv.coast_on && !dv.coast_from_data && dv.t < 6.f && dv.t >= dv.reprobe_t) {
        dv.reprobe_t = dv.t + 0.75f;
        int land = dv.land, relief = dv.relief;
        probe_coast();
        if (dv.coast_from_data && (land != dv.land || relief != dv.relief)) { LOG_INFO("gfx", "battle coast re-read from the package: rebuilding the stage"); stage_free(); stage_battle(); }
    }

    // instances (local coords offset to the scene centre)
    for (int t = 0; t < SHIP_COUNT; ++t) instlist_clear(&dv.ships_il[t]);
    scene_assets_begin();                                  // one bracket: staged ships and buildings share it
    shipyard_frame_begin();
    for (int i = 0; i < dv.nships; ++i) { const dv_ship_t *s = &dv.ship[i];
        float wx = dv.cx + s->x, wz = dv.cz + s->z, rock = dv.mode == DV_BATTLE ? 0.8f : 0.3f;
        float broll = 0.f, bpitch = 0.f;
        if (s->body >= 0) { float hv; nphys_attitude(s->body, &hv, &bpitch, &broll);
            // 0.22.12: the body's own roll and pitch go to the shader (inst_t roll_b/pitch_b), so
            // the canned motion is cut to a third and only adds the small short-period fidget
            rock = 0.28f * (1.f + 0.9f * nphys_heel(s->body)); }
        // Authored hull, rig and oars at the staged scene's magnification; the procedural hull
        // below stands in while a class is still loading (shipyard.h).
        float dx = wx - cam.pos.x, dz = wz - cam.pos.z;
        shipyard_req_t sr = { s->type, s->nation, ROLE_WARSHIP, wx, wz, s->heading, DV_SHIP_SCALE,
                              rock, 0.f, 0.f, s->r, s->g, s->b,
                              sqrtf(dx*dx + dz*dz + cam.pos.y*cam.pos.y), lod_key(LODK_MISC, 7, i), broll, bpitch, s->furl };
        // 0.22.20 (issue16 "mesh damage removal"): the masts the physics took are not drawn;
        // 0.22.21 (issue17): and a wounded or burning rig loses her canvas from the top down
        if (s->body >= 0) { nphys_damage_t dd; nphys_damage_get(s->body, &dd);
            sr.masts_down = dd.masts_down; sr.rig_d = maxf(dd.rig, dd.fire);
            sr.hull_d = dd.hull;                                      // 0.22.23 (issue19): visible damage on the planking
            for (int k = 0; k < 4; ++k) sr.mast_fall[k] = s->mast_fall[k]; }
        else { for (int k = 0; k < 4; ++k) sr.mast_fall[k] = -1.f; }
        if (shipyard_push(&sr)) continue;
        *instlist_push(&dv.ships_il[s->type]) = inst_t{ wx, 0, wz, s->heading,
            ship_scale(s->type) * DV_SHIP_SCALE, s->r, s->g, s->b, rock, 0, 1, 0 }; }
    shipyard_upload();
    for (int t = 0; t < SHIP_COUNT; ++t) mesh_set_instances(&ship_mesh[t][0], dv.ships_il[t].v, dv.ships_il[t].n);
    // 0.22.14 ("make cannonballs"): the round shot in the air (sim/naval_physics.h nphys_shot_list)
    // as small dark iron spheres, 0.13 local units (~32 cm, a little over life size so a ball
    // reads at the battle camera), drawn by the mesh pipeline with the ships
    {
        if (!shot_built) {
            mb_t m = mb_t{}; mb_material(&m, 0.55f, 0.6f);
            const float ir[3] = { 0.09f, 0.085f, 0.08f };
            // an octahedron subdivided once (32 faces), pushed onto the unit sphere
            float oct[6][3] = { {0,1,0},{0,-1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1} };
            int tri[8][3] = { {0,2,4},{0,4,3},{0,3,5},{0,5,2},{1,4,2},{1,3,4},{1,5,3},{1,2,5} };
            for (int f = 0; f < 8; ++f) {
                float *a = oct[tri[f][0]], *b = oct[tri[f][1]], *c = oct[tri[f][2]];
                float ab[3], bc[3], ca[3];
                for (int k = 0; k < 3; ++k) { ab[k] = a[k] + b[k]; bc[k] = b[k] + c[k]; ca[k] = c[k] + a[k]; }
                auto nz = [](float *v) { float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); v[0] /= l; v[1] /= l; v[2] /= l; };
                nz(ab); nz(bc); nz(ca);
                mb_tri(&m, a[0],a[1],a[2], ab[0],ab[1],ab[2], ca[0],ca[1],ca[2], ir[0], ir[1], ir[2], 0.f);
                mb_tri(&m, ab[0],ab[1],ab[2], b[0],b[1],b[2], bc[0],bc[1],bc[2], ir[0], ir[1], ir[2], 0.f);
                mb_tri(&m, ca[0],ca[1],ca[2], bc[0],bc[1],bc[2], c[0],c[1],c[2], ir[0], ir[1], ir[2], 0.f);
                mb_tri(&m, ab[0],ab[1],ab[2], bc[0],bc[1],bc[2], ca[0],ca[1],ca[2], ir[0], ir[1], ir[2], 0.f);
            }
            shot_mesh = mesh_upload(&m); shot_built = 1;
        }
        // 0.22.19 (issue15 "add real cannonballs ... and sizes depending of cannons"): the
        // ball's own radius comes back with its position, so a lower-deck 32-pounder's shot
        // is visibly the heavier one. Drawn at three times life size - a 15 cm ball at a
        // battle camera a hundred metres off is a pixel, and the point of drawing shot at all
        // is that the player can follow it.
        float xyzr[4 * 256]; int ns = nphys_shot_list(xyzr, 256);
        instlist_clear(&shot_il);
        for (int i = 0; i < ns; ++i) {
            inst_t si = inst_t{}; si.x = dv.cx + xyzr[i*4]; si.y = xyzr[i*4+1]; si.z = dv.cz + xyzr[i*4+2];
            si.scale = xyzr[i*4+3] * 3.0f; si.r = si.g = si.b = 1.f; si.bob = 0.f; si.sway = 0.f;   // no swell on a ball in flight
            *instlist_push(&shot_il) = si;
        }
        mesh_set_instances(&shot_mesh, shot_il.v, shot_il.n);
    }
    { float fv[24 * 4], fl[24]; int n = 0;                 // the staged ships foam too (sea_set_ships)
      for (int i = 0; i < dv.nships && n < 24; ++i) { const dv_ship_t *s = &dv.ship[i];
          fv[n*4] = dv.cx + s->x; fv[n*4+1] = dv.cz + s->z; fv[n*4+2] = s->heading; fv[n*4+3] = s->speed / s->len; fl[n] = s->len; n++; }   // speed in hull lengths/s
      sea_set_ships(fv, fl, n);
      // the wake map (gfx/wake.cc, 0.22.9): the trail every ship leaves on the water, painted
      // where she is now and faded over a minute or two. On the battle clock, so a stopped
      // battle holds its wakes exactly where they are.
      wake_update(dv.mode == DV_BATTLE ? dt * dv.timescale : dt * 0.35f, fv, fl, n); }
    if (dv.mode == DV_CITY) {
        // offset the prebuilt lists to the centre once per frame (cheap: a few hundred)
        static instlist_t sh[6];
        instlist_t *src[6] = { &dv.houses, &dv.roofs, &dv.towers, &dv.walls, &dv.piers, &dv.flags };
        mesh_t *dst[6] = { &mesh_house, &mesh_roof, &mesh_tower, &mesh_wall, &mesh_pier, &mesh_flagpole };
        for (int l = 0; l < 6; ++l) {
            instlist_clear(&sh[l]);
            for (int i = 0; i < src[l]->n; ++i) { inst_t in = src[l]->v[i]; in.x += dv.cx; in.z += dv.cz; *instlist_push(&sh[l]) = in; }
            mesh_set_instances(dst[l], sh[l].v, sh[l].n);
        }
        asset_t *church = asset_get_role(ASSET_BUILDING, "church");
        if (church) {
            inst_t in = { dv.cx + 34, 0.05f, dv.cz - 14, 0.3f, 9.f / maxf(church->bmax[1] - church->bmin[1], 0.1f), 1,1,1, 0,0,0,0 };
            scene_asset_instance(church, in, lod_key(LODK_MISC, 1, 0), dist2d(dv.cx + 34, dv.cz - 14, cam.pos.x, cam.pos.z));
        }
        for (int i = 0; i < dv.nbuild; ++i) {
            asset_t *a = asset_at(dv.build[i].asset);
            if (!a || a->state != ASSET_LOADED) continue;
            inst_t in = dv.build[i].in; in.x += dv.cx; in.z += dv.cz;
            float dx = in.x - cam.pos.x, dz = in.z - cam.pos.z;
            scene_asset_instance(a, in, lod_key(LODK_MISC, 2, i), sqrtf(dx*dx + dz*dz + cam.pos.y*cam.pos.y));
        }
    } else {
        mesh_set_instances(&mesh_house, 0, 0); mesh_set_instances(&mesh_roof, 0, 0); mesh_set_instances(&mesh_tower, 0, 0);
        mesh_set_instances(&mesh_wall, 0, 0); mesh_set_instances(&mesh_pier, 0, 0); mesh_set_instances(&mesh_flagpole, 0, 0);
    }
    scene_assets_end();
    const inst_t stage_in = inst_t{ dv.cx, 0, dv.cz, 0, 1, 1,1,1, 0,0,0,0 };   // (C compound literal -> named: C++ cannot take &T{...})
    mesh_set_instances(&dv.stage, &stage_in, 1);
}

int DetailView::submit(void) {
    if (dv.phase == DVP_IDLE || dv.phase == DVP_OUT || !dv.stage_built) return 0;
    int n = 0;
    mesh_draw(&dv.stage); n++;
    for (int t = 0; t < SHIP_COUNT; ++t) { mesh_draw(&ship_mesh[t][0]); n += ship_mesh[t][0].ninst; }
    if (shot_built && shot_il.n) { mesh_draw(&shot_mesh); n += shot_il.n; }   // the round shot (0.22.14)
    if (dv.mode == DV_CITY) {
        mesh_draw(&mesh_house); mesh_draw(&mesh_roof); mesh_draw(&mesh_tower);
        mesh_draw(&mesh_wall); mesh_draw(&mesh_pier); mesh_draw(&mesh_flagpole);
        n += mesh_house.ninst + mesh_roof.ninst + mesh_tower.ninst + mesh_wall.ninst + mesh_pier.ninst + mesh_flagpole.ninst;
    }
    n += scene_assets_submit();   // authored hulls, rigs and oars in both modes; the city's buildings too
    n += shipyard_submit();       // procedural rigging and canvas
    return n;
}

// The staged scene as shadow casters: the same stage and ships, but the authored buildings
// come from their coarse caster meshes (scene.c, DESIGN_VISUAL.md step 3).
int DetailView::submit_shadow(void) {
    if (dv.phase == DVP_IDLE || dv.phase == DVP_OUT || !dv.stage_built) return 0;
    int n = 0;
    mesh_draw(&dv.stage); n++;
    for (int t = 0; t < SHIP_COUNT; ++t) { mesh_draw(&ship_mesh[t][0]); n += ship_mesh[t][0].ninst; }
    if (dv.mode == DV_CITY) {
        mesh_draw(&mesh_house); mesh_draw(&mesh_roof); mesh_draw(&mesh_tower);
        mesh_draw(&mesh_wall); mesh_draw(&mesh_pier); mesh_draw(&mesh_flagpole);
        n += mesh_house.ninst + mesh_roof.ninst + mesh_tower.ninst + mesh_wall.ninst + mesh_pier.ninst + mesh_flagpole.ninst;
    }
    n += scene_assets_submit_shadow();   // authored hulls cast in both modes
    n += shipyard_submit_shadow();
    return n;
}

// the gunsmoke: the particle system's own pipeline (particles.h), after the water
int DetailView::submit_smoke(void) { return particles_draw(); }

// Wind arrows over the water (0.22.7, issue4 "wind arrows in battle mode"): a lattice around
// the look-at point, each arrow sliding downwind on the battle clock, none over the stage land.
void DetailView::overlays(void) {
    if (mode() != DV_BATTLE || !dv.show_wind) return;
    float step = maxf(9.f, cam.dist * 0.34f + 5.f);
    float x0 = floorf((cam.tx - step * 4) / step) * step, z0 = floorf((cam.tz - step * 3) / step) * step;
    float l = step * 0.30f * clampf(0.45f + 0.55f * dv.wstr, 0.4f, 1.2f);
    unsigned col = rgb_linear(rgb(236, 240, 246));
    for (int j = 0; j < 7; ++j) for (int i = 0; i < 9; ++i) {
        float x = x0 + i * step, z = z0 + j * step;
        if (dv.coast_on) {   // over the land? the same profile the water uses
            float dx = x - (dv.cx + dv.cnx * dv.coast_dist), dz = z - (dv.cz + dv.cnz * dv.coast_dist);
            float v = dx * dv.cnx + dz * dv.cnz, u = dx * -dv.cnz + dz * dv.cnx;
            if (v - shore_wiggle(u) > -4.f) continue;
        }
        float ph = fmodf(RF.anim_time * 0.45f + i * 0.37f + j * 0.61f, 1.f);
        vec3 a = vec3(x + dv.wdx * l * (ph - 0.5f), 1.4f, z + dv.wdz * l * (ph - 0.5f)), b = vec3(a.x + dv.wdx * l * 0.5f, 1.4f, a.z + dv.wdz * l * 0.5f);
        dd_arrow(a, b, dd_opts().with_color(col));
    }
}

// The open sea has no horizon to speak of (issue4 "fog of war in the distance"): past the
// fleets the haze takes the water into the sky, and with a coast the air is a little thicker
// than the map's so the hills recede. Called after the weather wrote RF.fog_* each frame.
void DetailView::weather(void) {
    if (mode() != DV_BATTLE) return;
    float d = RF.fog_density > 0 ? RF.fog_density : 1.f, st = RF.fog_start > 0 ? RF.fog_start : 250.f;
    if (!dv.coast_on) { RF.fog_density = d * 2.3f; RF.fog_start = minf(st, 60.f); RF.vfog_density += 0.0012f; }
    else { RF.fog_density = d * 1.1f; RF.fog_start = minf(st, 160.f); }
}

// the eye never dips into the staged water: a wave and a bit above the mean surface
float DetailView::eye_floor(void) { return 1.2f; }

// The trailer camera (0.22.10): a 45-second move over the battle, keyed in local terms - the
// distance, the yaw, the look-at offset from the scene centre, the pitch scale, the look-at
// height - and eased between marks. Driven by the recorder's clock (main.cc MARE_RECORD) so a
// take at 60 fps and a take at 24 fps make the same film. Opens wide over the coast, sweeps
// down the line, closes on a broadside, rises over the fleet and pulls away into the haze.
void DetailView::trailer(float t) {
    if (mode() != DV_BATTLE) return;
    struct key_t { float t, dist, yaw, ox, oz, pitch, ty; };
    // the lines run along x at z = -14 (side 0) and +14 (side 1), engaged through the centre:
    // yaw 90 looks along +x down the lanes, 55 and 125 are the three-quarter views of them
    static const key_t K[] = {
        {  0.f, 190.f,  55.f,   0.f,   0.f, 0.38f,  0.f },
        {  9.f, 105.f,  78.f,  10.f,  -4.f, 0.46f,  0.f },
        { 17.f,  36.f, 102.f,  12.f, -13.f, 0.64f,  0.f },
        { 25.f,  29.f, 152.f,  -6.f,  11.f, 0.72f,  1.5f },
        { 33.f,  68.f, 202.f,   0.f,   0.f, 0.56f,  6.f },
        { 40.f, 140.f, 246.f,   0.f,   0.f, 0.46f, 18.f },
        { 45.f, 205.f, 276.f,   0.f, -10.f, 0.38f,  0.f },
    };
    const int N = (int)(sizeof K / sizeof K[0]);
    float tt = t < 0.f ? 0.f : t;
    int i = 0; while (i + 2 < N && tt >= K[i + 1].t) ++i;
    const key_t &a = K[i], &b = K[i + 1];
    float u = clampf((tt - a.t) / maxf(b.t - a.t, 1e-3f), 0.f, 1.f); u = u * u * (3.f - 2.f * u);
    float dist = lerpf(a.dist, b.dist, u), yaw = lerpf(a.yaw, b.yaw, u) * 0.0174533f;
    float ox = lerpf(a.ox, b.ox, u), oz = lerpf(a.oz, b.oz, u), ps = lerpf(a.pitch, b.pitch, u), ty = lerpf(a.ty, b.ty, u);
    cam.dist = cam.want_dist = dist; cam.yaw = cam.want_yaw = yaw;
    cam.tx = cam.want_tx = dv.cx + ox; cam.tz = cam.want_tz = dv.cz + oz; cam.ty = cam.want_ty = ty;
    cam_pitch_scale = ps;
}

// ---------------------------------------------------------------------------
// cloud overlay: procedural fbm clouds over the finished frame (after the tonemap)


void DetailView::clouds(void) {
    float cover = dv_cover();
    if (cover <= 0.001f) return;
    if (!dv.cloud_init) {
        static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
        static const rhi_layout_t layout = { attrs, 1, { 2 * sizeof(float), 0 } };
        static const char *uniforms[] = { "u_cover", "u_time", "u_aspect", 0 };
        const rhi_pipeline_desc_t desc{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("detailview/clouds.frag"), .layout = &layout, .uniforms = uniforms, .blend = RHI_BLEND_ALPHA };   // (C compound literal -> named: C++ cannot take &T{...})
        dv.cloud_prg = rhi_pipeline_make(&desc);
        float tri[6] = { -1, -1, 3, -1, -1, 3 };
        dv.cloud_geo = rhi_geometry_make(&layout, tri, 3, 0, 0);
        dv.cloud_init = 1;
    }
    rhi_pipeline_bind(&dv.cloud_prg);
    rhi_set_f1(UNI(dv.cloud_prg, "u_cover"), clampf(cover, 0, 1));
    rhi_set_f1(UNI(dv.cloud_prg, "u_time"), RF.time);
    rhi_set_f1(UNI(dv.cloud_prg, "u_aspect"), screen_w / maxf(1.f, (float)screen_h));
    rhi_draw(&dv.cloud_geo, 0);
}

} // namespace mare::game::view

// C ABI (staged_scene.h) for the unported callers
extern "C" {
int  detailview_mode(void) { return mare::game::view::detail_view().mode(); }
int  detailview_busy(void) { return mare::game::view::detail_view().busy(); }
void detailview_enter_battle(int fleet_id) { mare::game::view::detail_view().enter_battle(fleet_id); }
void detailview_enter_city(int port) { mare::game::view::detail_view().enter_city(port); }
void detailview_exit(void) { mare::game::view::detail_view().exit(); }
void  detailview_set_fire_order(int o) { mare::game::view::detail_view().set_fire_order(o); }
void  detailview_set_move_order(int o) { mare::game::view::detail_view().set_move_order(o); }
void  detailview_set_furl_order(int o) { mare::game::view::detail_view().set_furl_order(o); }
int   detailview_ship_info(int i, dv_shipinfo_t *out) { return mare::game::view::detail_view().ship_info(i, out); }
int   detailview_ship_count(void) { return mare::game::view::detail_view().ship_count(); }
int   detailview_furl_order(void) { return mare::game::view::detail_view().furl_order(); }
int   detailview_fire_order(void) { return mare::game::view::detail_view().fire_order(); }
int   detailview_move_order(void) { return mare::game::view::detail_view().move_order(); }
void  detailview_helm(float d) { mare::game::view::detail_view().helm(d); }
void  detailview_helm_release(void) { mare::game::view::detail_view().helm_release(); }
int   detailview_helm_on(void) { return mare::game::view::detail_view().helm_on(); }
float detailview_helm_course(void) { return mare::game::view::detail_view().helm_course(); }
void detailview_abort(void) { mare::game::view::detail_view().helm_release(); mare::game::view::detail_view().abort(); }
int  detailview_hits(void) { return mare::game::view::detail_view().hits(); }
void detailview_frame(float dt) { mare::game::view::detail_view().frame(dt); }
int  detailview_submit(void) { return mare::game::view::detail_view().submit(); }
int  detailview_submit_shadow(void) { return mare::game::view::detail_view().submit_shadow(); }
int  detailview_submit_smoke(void) { return mare::game::view::detail_view().submit_smoke(); }
void detailview_sea(void) { mare::game::view::detail_view().sea(); }
void detailview_clouds(void) { mare::game::view::detail_view().clouds(); }
float detailview_timescale(void) { return mare::game::view::detail_view().timescale(); }
void  detailview_set_timescale(float s) { mare::game::view::detail_view().set_timescale(s); }
float detailview_clock_scale(void) { return mare::game::view::detail_view().clock_scale(); }
void  detailview_wind(float *dx, float *dz, float *str) { mare::game::view::detail_view().wind(dx, dz, str); }
int   detailview_show_wind(void) { return mare::game::view::detail_view().show_wind(); }
void  detailview_toggle_wind(void) { mare::game::view::detail_view().toggle_wind(); }
const char *detailview_coast_desc(void) { return mare::game::view::detail_view().coast_desc(); }
void  detailview_overlays(void) { mare::game::view::detail_view().overlays(); }
void  detailview_weather(void) { mare::game::view::detail_view().weather(); }
float detailview_eye_floor(void) { return mare::game::view::detail_view().eye_floor(); }
void  detailview_trailer(float t) { mare::game::view::detail_view().trailer(t); }
}
