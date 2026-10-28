// Detail view (see detailview.h). Everything here is presentation: the scenes are built from
// the simulation state at entry (engaged fleets, port size/buildings/population) and then live
// their own gentle life - ships sail, turn and trade broadsides, flags and smoke move - without
// writing anything back.
//
// Local scale: 1 unit ~ 4 m. Ships reuse the world meshes at DV_SHIP_SCALE, which puts a sloop
// at ~24 m and a ship of the line at ~52 m; the battle coast sits DV_COAST_DIST (~1 km) away.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "detailview.h"
#include "render.h"
#include "camera.h"
#include "scene.h"
#include "glsl.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../assets/asset.h"
#include "../data/tables.h"
#include "../sim/fleet.h"
#include "../sim/settlement.h"
#include "lod.h"

#define DV_SHIP_SCALE   9.f          // world ship mesh -> local units (1 unit ~ 2.8 m: a ship of the line is ~19 units)
#define DV_COAST_DIST 190.f          // battle: coast this far north (+z is south, so -z)
#define DV_MAX_SHIPS   50
#define DV_MAX_BUILD  160
#define DV_SEA_DEPTH   40.f          // staged scenes have no heightmap: open water everywhere
typedef struct dv_build_t dv_build_t;
#define DV_CLOUD_IN   0.8f           // seconds of cloud cover building up
#define DV_CLOUD_OUT  1.1f           // seconds of reveal (the camera dives during this)

enum { DVP_IDLE, DVP_IN, DVP_SHOW, DVP_OUT_IN, DVP_OUT };  // transition phases: IN -> (swap) -> SHOW; exit: OUT_IN -> (restore) -> OUT

typedef struct dv_ship_t {
    int   type, side;
    float x, z, heading, speed, phase;
    float r, g, b;
    float next_fire;
} dv_ship_t;

typedef struct dv_puff_t { float x, y, z, age, scale; } dv_puff_t;

static struct {
    int mode, want_mode;             // active mode and the mode being faded in
    int phase; float t;              // transition phase + its clock
    float cx, cz;                    // scene centre (world coords, world passes are off anyway)
    float saved_tx, saved_tz, saved_dist, saved_yaw;
    int want_port, want_fleet;
    // battle
    dv_ship_t ship[DV_MAX_SHIPS]; int nships;
    dv_puff_t puff[96]; int npuffs;
    float time;
    // stage + instance lists
    mesh_t stage; int stage_built;
    struct dv_build_t { short asset; inst_t in; } build[DV_MAX_BUILD]; int nbuild;   // authored houses (registry index + local instance)
    instlist_t ships_il[SHIP_COUNT], houses, roofs, towers, walls, piers, flags, puffs_il;
    // clouds
    rhi_pipeline_t cloud_prg; rhi_geometry_t cloud_geo; int cloud_init;
} dv;

// The world keeps rendering while the clouds roll in over the map (IN) and after the camera is
// restored (OUT); the staged scene owns the frame during SHOW and the covering half of the exit.
int detailview_mode(void) { return dv.phase == DVP_SHOW || dv.phase == DVP_OUT_IN ? dv.mode : DV_OFF; }
int detailview_busy(void) { return dv.phase == DVP_IN || dv.phase == DVP_OUT_IN; }

// ---------------------------------------------------------------------------
// stage meshes (ground, sea, coast), built with the ordinary mesh builder so they receive
// shadows and PBR from the standard pipeline

static void stage_free(void) { dv.stage_built = 0; }   // geometry is leaked per entry (tiny); rebuilt each time

static void stage_battle(void) {
    mb_t m = {0};
    float S = 1500;
    // (no sea quad: sea_draw_local draws the real water, with the same waves the map has)
    // the coast, a kilometre to the north: beach strip, grass rise, a few worn hills
    mb_material(&m, MB_STONE);
    float zc = -DV_COAST_DIST;
    mb_quad(&m, (float[]){-S,0.08f,zc}, (float[]){S,0.08f,zc}, (float[]){S,0.3f,zc-35}, (float[]){-S,0.3f,zc-35}, 0.80f,0.74f,0.55f, 0);     // sand
    mb_quad(&m, (float[]){-S,0.3f,zc-35}, (float[]){S,0.3f,zc-35}, (float[]){S,6,zc-160}, (float[]){-S,6,zc-160}, 0.36f,0.52f,0.28f, 0);     // grass rise
    mb_quad(&m, (float[]){-S,6,zc-160}, (float[]){S,6,zc-160}, (float[]){S,8,zc-600}, (float[]){-S,8,zc-600}, 0.33f,0.46f,0.27f, 0);
    unsigned seed = 7;
    for (int i = 0; i < 9; ++i) {                                   // hills as faceted mounds
        float hx = -500 + i * 120 + rnd_f(&seed) * 60, hz = zc - 90 - rnd_f(&seed) * 220;
        float hr = 45 + rnd_f(&seed) * 70, hh = 18 + rnd_f(&seed) * 30;
        float g = 0.30f + rnd_f(&seed) * 0.12f;
        for (int k = 0; k < 7; ++k) {
            float a0 = k * 6.2832f / 7, a1 = (k+1) * 6.2832f / 7;
            mb_tri(&m, hx + cosf(a0)*hr, 1, hz + sinf(a0)*hr, hx + cosf(a1)*hr, 1, hz + sinf(a1)*hr, hx, hh, hz, g*1.2f, g*1.35f, g*0.8f, 0);
        }
    }
    dv.stage = mesh_upload(&m);
    dv.stage_built = 1;
}

static void stage_city(void) {
    mb_t m = {0};
    float S = 700;
    mb_material(&m, MB_STONE);
    mb_quad(&m, (float[]){-S,0,S}, (float[]){S,0,S}, (float[]){S,0,-120}, (float[]){-S,0,-120}, 0.38f,0.50f,0.28f, 0);     // grass
    mb_quad(&m, (float[]){-S,0.02f,-120}, (float[]){S,0.02f,-120}, (float[]){S,0.02f,-160}, (float[]){-S,0.02f,-160}, 0.80f,0.74f,0.55f, 0);  // beach
    mb_material(&m, MB_STONE);
    mb_quad(&m, (float[]){-90,0.05f,90}, (float[]){90,0.05f,90}, (float[]){90,0.05f,-90}, (float[]){-90,0.05f,-90}, 0.55f,0.52f,0.47f, 0);    // cobbled town ground
    dv.stage = mesh_upload(&m);
    dv.stage_built = 1;
}

// ---------------------------------------------------------------------------
// battle scene: one entry per ship, each with its own course, speed and gun timer

static void battle_add_ship(int type, int side, float x, float z, float heading, unsigned col, unsigned *seed) {
    if (dv.nships >= DV_MAX_SHIPS) return;
    dv_ship_t *s = &dv.ship[dv.nships++];
    *s = (dv_ship_t){ type, side, x, z, heading, (0.9f + rnd_f(seed) * 0.9f) * (0.55f + 0.1f * type), rnd_f(seed) * 6.28f,
                      col_r(col), col_g(col), col_b(col), 3.f + rnd_f(seed) * 6.f };
}

static void build_battle(int fleet_id) {
    dv.nships = 0; dv.npuffs = 0; dv.time = 0;
    unsigned seed = 1234;
    // find the battle: the chosen fleet, else the freshest firing fleet
    const fleet_t *hot = fleet_by_id(fleet_id);
    if (!hot || !hot->alive) { hot = 0;
        for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i];
            if (f->alive && f->at_port < 0 && f->last_fire > G.day - 1.f && (!hot || f->last_fire > hot->last_fire)) hot = f; } }
    int real = 0;
    if (hot) {   // everyone in the fight: fleets near the hot one that fired recently (both sides)
        for (int i = 0; i < G.nfleets && dv.nships < DV_MAX_SHIPS; ++i) { const fleet_t *f = &G.fleets[i];
            if (!f->alive || f->at_port >= 0 || dist2d(f->x, f->z, hot->x, hot->z) > 12.f) continue;
            if (f->last_fire < G.day - 1.f && f->id != hot->id) continue;
            int side = f->nation == hot->nation ? 0 : 1; real = 1;
            unsigned col = f->role == ROLE_PIRATE ? rgb(25,25,25) : nations_info[f->nation].color;
            for (int k = 0; k < f->nships; ++k) {
                float lx = -46 + (dv.nships % 7) * 15.f + rnd_f(&seed) * 7, lz = (side ? 14 : -14) + ((dv.nships / 7) % 3) * 19.f + rnd_f(&seed) * 8;
                battle_add_ship(f->ships[k].type, side, lx, lz, side ? 3.14159f + rnd_f(&seed) * 0.5f : rnd_f(&seed) * 0.5f, col, &seed);
            }
        }
    }
    if (!real) {   // no battle under way: stage a sample exercise matching the reference framing
        unsigned ca = nations_info[G.player >= 0 ? G.player : 1].color, cb = rgb(140, 25, 25);
        for (int i = 0; i < 15; ++i) {
            int ta = i % SHIP_COUNT, tb = (i * 2 + 1) % SHIP_COUNT;
            battle_add_ship(ta, 0, -56 + (i % 5) * 22 + rnd_f(&seed) * 8, -22 - (i / 5) * 20 + rnd_f(&seed) * 6, 0.25f + rnd_f(&seed) * 0.4f, ca, &seed);
            battle_add_ship(tb, 1, -48 + (i % 5) * 23 + rnd_f(&seed) * 8,  20 + (i / 5) * 19 + rnd_f(&seed) * 6, 3.0f + rnd_f(&seed) * 0.4f, cb, &seed);
        }
        LOG_INFO("gfx", "detail view: no battle under way, staged a 30-ship exercise");
    } else LOG_INFO("gfx", "detail view: battle with %d ships", dv.nships);
    stage_battle();
}

static void battle_update(float dt) {
    dv.time += dt;
    unsigned seed = (unsigned)(dv.time * 977) + 13;
    for (int i = 0; i < dv.nships; ++i) {
        dv_ship_t *s = &dv.ship[i];
        s->heading += sinf(dv.time * 0.21f + s->phase) * 0.055f * dt * 8;     // lazy individual course changes
        // keep the melee inside the stage: bend towards the centre when straying
        float d = sqrtf(s->x * s->x + s->z * s->z);
        if (d > 70) { float want = atan2f(-(0 - s->z), 0 - s->x); s->heading += wrap_angle(want - s->heading) * 0.5f * dt; }
        s->x += cosf(s->heading) * s->speed * dt * 2.f;
        s->z -= sinf(s->heading) * s->speed * dt * 2.f;                       // heading convention matches inst_t (0 = +x)
        s->next_fire -= dt;
        if (s->next_fire < 0) {                                                // broadside at the nearest enemy within range
            s->next_fire = 4.f + rnd_f(&seed) * 7.f;
            for (int j = 0; j < dv.nships; ++j) {
                if (dv.ship[j].side == s->side) continue;
                if (dist2d(s->x, s->z, dv.ship[j].x, dv.ship[j].z) > 55.f) continue;
                for (int k = 0; k < 3 && dv.npuffs < 96; ++k) {
                    float side = (j % 2) ? 1.f : -1.f;
                    dv_puff_t *p = &dv.puff[dv.npuffs++];
                    *p = (dv_puff_t){ s->x - sinf(s->heading) * side * (2.5f + k * 1.3f), 2.4f + k * 0.6f,
                                      s->z - cosf(s->heading) * side * (2.5f + k * 1.3f), 0, 2.2f + k };
                }
                break;
            }
        }
    }
    for (int i = 0; i < dv.npuffs; ) {
        dv.puff[i].age += dt;
        dv.puff[i].y += dt * 1.1f;
        if (dv.puff[i].age > 3.2f) dv.puff[i] = dv.puff[--dv.npuffs]; else ++i;
    }
}

// ---------------------------------------------------------------------------
// city scene: districts laid out from the port's size, population and buildings
// (harbour at the waterfront, commercial centre, residential mass, church, walls when a fort
//  exists, farms outside) - the reference layout of Designer (14) at 1:1

#define DV_HOUSE 3.2f       // base house scale: ~13 m footprint
static float dv_fp[220][3]; static int dv_nfp;
static const char *const dv_roles_rich[] = { "wealthy_townhouse", "merchant_house", "three_story_townhouse", "inn" };
static const char *const dv_roles_mid[]  = { "two_story_townhouse", "artisan_house", "shop", "bakery", "blacksmith", "small_stone_house" };
static const char *const dv_roles_poor[] = { "poor_house", "small_wooden_house", "small_stone_house" };
static asset_t *dv_house_asset(float ring, int size, unsigned *seed) {
    const char *const *roles; int n;
    float tier = ring + (rnd_f(seed) - 0.5f) * 0.35f;
    if (tier < 0.3f && size >= 2) { roles = dv_roles_rich; n = 4; } else if (tier < 0.7f) { roles = dv_roles_mid; n = 6; } else { roles = dv_roles_poor; n = 3; }
    int start = rnd_int(seed, n);
    for (int k = 0; k < n; ++k) { asset_t *a = asset_get_role(ASSET_BUILDING, roles[(start + k) % n]); if (a) return a; }
    return NULL;
}
static int dv_free(float x, float z, float r) {
    for (int i = 0; i < dv_nfp; ++i) if (dist2d(dv_fp[i][0], dv_fp[i][1], x, z) < dv_fp[i][2] + r) return 0;
    return 1;
}
static void dv_take(float x, float z, float r) { if (dv_nfp < 220) { dv_fp[dv_nfp][0]=x; dv_fp[dv_nfp][1]=z; dv_fp[dv_nfp][2]=r; dv_nfp++; } }

static void build_city(int port) {
    dv.nships = 0; dv.npuffs = 0; dv_nfp = 0;
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
    *instlist_push(&dv.towers) = (inst_t){ 6, 0, -8, 0.3f, 4.f + size, 0.8f,0.78f,0.72f, 0,0,0,0 };  dv_take(6, -8, 6);
    *instlist_push(&dv.flags)  = (inst_t){ -6, 0, 2, 0, 8.f + size, col_r(col), col_g(col), col_b(col), 0,0,0,0 }; dv_take(-6, 2, 3);
    // (the church - the religious district - is placed at draw time from the shared asset mesh)
    dv_take(34, -14, 14);                                      // reserve the church plot east of the plaza
    // harbour district: piers into the water, warehouses on the front
    for (int i = 0; i < docks + 1; ++i) {
        float px = -60 + i * 38.f + rnd_f(&seed) * 8;
        *instlist_push(&dv.piers) = (inst_t){ px, 0, -164, 1.5708f, 4.f, 1,1,1, 0,0,0, 12.f };
    }
    for (int i = 0; i < whs * 2 + 1; ++i) {
        float wx = -70 + i * 26.f + rnd_f(&seed) * 10, wz = -128 + rnd_f(&seed) * 10;
        float s = DV_HOUSE * 1.5f;
        if (!dv_free(wx, wz, s * 0.6f)) continue;
        *instlist_push(&dv.houses) = (inst_t){ wx, 0, wz, 1.5708f, s, 0.60f,0.52f,0.42f, 0,0,0,0 };
        *instlist_push(&dv.roofs)  = (inst_t){ wx, s, wz, 1.5708f, s, 0.35f,0.33f,0.30f, 0,0,0,0 };
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
            dv.build[dv.nbuild++] = (dv_build_t){ (short)(house - asset_at(0)), { hx, 0.02f, hz, -atan2f(-hz, -hx) + (rnd_f(&seed) - 0.5f) * 0.5f, sc, 1,1,1, 0,0,0,0 } };
        } else {
            float wc = 0.72f + rnd_f(&seed) * 0.2f;
            int commercial = r < 45 && market;
            *instlist_push(&dv.houses) = (inst_t){ hx, 0, hz, rnd_f(&seed) * 3.14f, s, wc, wc * (commercial ? 0.95f : 0.9f), wc * 0.75f, 0,0,0,0 };
            *instlist_push(&dv.roofs)  = (inst_t){ hx, s, hz, dv.houses.v[dv.houses.n-1].heading, s, 0.66f, 0.33f, 0.20f, 0,0,0,0 };
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
            *instlist_push(&dv.walls) = (inst_t){ (x0+x1)/2, 0, (z0+z1)/2, -atan2f(z1-z0, x1-x0), 2.6f + 0.5f * fort, 1,1,1, 0,0,0, dist2d(x0,z0,x1,z1) / (2.6f + 0.5f * fort) };
            if (k % 6 == 0) *instlist_push(&dv.towers) = (inst_t){ x0, 0, z0, 0, 3.f + 0.5f * fort, 0.75f,0.73f,0.68f, 0,0,0,0 };
        }
    }
    // outer farms: huts scattered beyond the walls
    for (int i = 0; i < 6 + 3 * size; ++i) {
        float a = rnd_f(&seed) * 3.5f - 0.3f, r = R + 25 + rnd_f(&seed) * 90;
        float hx = cosf(a) * r, hz = sinf(a) * r * 0.82f - 20;
        if (hz < -100) continue;
        float s = DV_HOUSE * 0.7f;
        if (!dv_free(hx, hz, s)) continue;
        *instlist_push(&dv.houses) = (inst_t){ hx, 0, hz, rnd_f(&seed) * 3.14f, s, 0.68f, 0.6f, 0.45f, 0,0,0,0 };
        *instlist_push(&dv.roofs)  = (inst_t){ hx, s, hz, dv.houses.v[dv.houses.n-1].heading, s, 0.45f, 0.40f, 0.30f, 0,0,0,0 };
        dv_take(hx, hz, s);
    }
    // a couple of ships moored off the piers for life
    unsigned scol = col;
    battle_add_ship(SHIP_FLUYT, 0, -40, -180, 0.2f, scol, &seed);
    battle_add_ship(SHIP_MERCHANT, 0, 25, -195, 2.9f, scol, &seed);
    for (int i = 0; i < dv.nships; ++i) dv.ship[i].speed *= 0.25f;
    stage_city();
    LOG_INFO("gfx", "detail view: city '%s' size %d pop %.0f: %d houses, %d walls, fort %d, market %d",
             p ? p->name : "(sample)", size, pop, dv.houses.n, dv.walls.n, fort, market);
}

// ---------------------------------------------------------------------------
// transitions

static void dv_begin(int mode, int port, int fleet) {
    if (dv.phase == DVP_IDLE) { dv.saved_tx = cam.want_tx; dv.saved_tz = cam.want_tz; dv.saved_dist = cam.want_dist; dv.saved_yaw = cam.want_yaw; }
    dv.want_mode = mode; dv.want_port = port; dv.want_fleet = fleet;
    dv.phase = DVP_IN; dv.t = 0;
}
void detailview_enter_battle(int fleet_id) { dv_begin(DV_BATTLE, -1, fleet_id); }
void detailview_enter_city(int port) {
    if (port < 0) { float best = 1e9f;
        for (int i = 0; i < G.nports; ++i) { const port_t *pp = &G.ports[i];
            if (!port_is_gone(pp) && pp->size >= 1) { float d = dist2d(pp->x, pp->z, cam.tx, cam.tz); if (d < best) { best = d; port = i; } } } }
    if (port >= 0) dv_begin(DV_CITY, port, 0);
}
void detailview_exit(void) { if (dv.phase == DVP_SHOW) { LOG_INFO("gfx", "detail view: leaving (t=%.1f)", dv.t); dv.phase = DVP_OUT_IN; dv.t = 0; } }

static float dv_cover(void) {   // cloud cover 0..1 for the current phase
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
static void dv_sea_params(void) {
    int on = dv.phase != DVP_IDLE && dv.phase != DVP_OUT;
    sea_wave_scale = on ? DV_SHIP_SCALE : 1.f;
    sea_local_depth = on ? DV_SEA_DEPTH : 0.f;
}
void detailview_sea(void) {
    // The battle is open ocean; the city's water stops at its beach (stage_city lays the sand
    // out to local z = -160), so the staged sea is clipped there instead of flooding the town.
    sea_clip_on = dv.mode == DV_CITY;
    sea_clip_z = dv.cz - 158.f;
    sea_draw_local(DV_SEA_DEPTH, DV_SHIP_SCALE, 1600.f);
    sea_clip_on = 0;
}

void detailview_frame(float dt) {
    dv_sea_params();
    if (dv.phase == DVP_IDLE) return;
    dv.t += dt;
    if (dv.phase == DVP_IN && dv.t >= DV_CLOUD_IN) {          // fully covered: swap to the staged scene
        dv.mode = dv.want_mode;
        if (dv.mode == DV_BATTLE) build_battle(dv.want_fleet); else build_city(dv.want_port);
        // local space, kept far enough inside the world box that the camera clamp never bites
        // (the z span of the world is only ~520 units, so the margin must stay well under half)
        dv.cx = clampf(cam.want_tx, lon2x(LON0) + 140, lon2x(LON1) - 140);
        dv.cz = clampf(cam.want_tz, lat2z(LAT1) + 140, lat2z(LAT0) - 140);
        LOG_INFO("gfx", "detail view: scene centred at %.0f %.0f (cam was %.0f %.0f)", dv.cx, dv.cz, cam.want_tx, cam.want_tz);
        cam.tx = cam.want_tx = dv.cx; cam.tz = cam.want_tz = dv.cz + (dv.mode == DV_BATTLE ? -25 : -30);
        cam.dist = 380; cam.want_dist = dv.mode == DV_BATTLE ? 32 : 110;      // the dive through the clouds
        cam.yaw = cam.want_yaw = dv.mode == DV_BATTLE ? 0.12f : 0.25f;        // battle: facing the coast a kilometre off
        cam_pitch_scale = 0.72f;                                               // towards the horizon, like the concept frames
        dv.phase = DVP_SHOW; dv.t = 0;
    }
    if (dv.phase == DVP_OUT_IN && dv.t >= DV_CLOUD_IN) {      // covered again: restore the map camera
        cam_pitch_scale = 1.f;
        cam.tx = cam.want_tx = dv.saved_tx; cam.tz = cam.want_tz = dv.saved_tz;
        cam.dist = cam.want_dist = dv.saved_dist; cam.yaw = cam.want_yaw = dv.saved_yaw;
        dv.phase = DVP_OUT; dv.t = 0;
    }
    if (dv.phase == DVP_OUT && dv.t >= DV_CLOUD_OUT) { dv.phase = DVP_IDLE; dv.mode = DV_OFF; stage_free(); return; }
    if (dv.phase != DVP_SHOW) return;

    cam.want_dist = clampf(cam.want_dist, 6, 260);            // stay under the map morph in local space
    cam.want_tx = clampf(cam.want_tx, dv.cx - 120, dv.cx + 120);
    cam.want_tz = clampf(cam.want_tz, dv.cz - 120, dv.cz + 120);

    if (dv.mode == DV_BATTLE) battle_update(dt);
    else battle_update(dt * 0.35f);                            // moored ships barely stir

    // instances (local coords offset to the scene centre)
    for (int t = 0; t < SHIP_COUNT; ++t) instlist_clear(&dv.ships_il[t]);
    instlist_clear(&dv.puffs_il);
    for (int i = 0; i < dv.nships; ++i) { const dv_ship_t *s = &dv.ship[i];
        *instlist_push(&dv.ships_il[s->type]) = (inst_t){ dv.cx + s->x, 0, dv.cz + s->z, s->heading,
            ship_scale(s->type) * DV_SHIP_SCALE, s->r, s->g, s->b, dv.mode == DV_BATTLE ? 0.8f : 0.3f, 0, 1, 0 }; }
    for (int i = 0; i < dv.npuffs; ++i) { const dv_puff_t *p = &dv.puff[i];
        float t01 = p->age / 3.2f, env = sinf(3.14159f * minf(t01, 1.f));
        *instlist_push(&dv.puffs_il) = (inst_t){ dv.cx + p->x, p->y, dv.cz + p->z, 0,
            (p->scale + t01 * 6.f) * env * 0.55f, 0.88f, 0.87f, 0.86f, 0, 0, 0, 0 }; }
    for (int t = 0; t < SHIP_COUNT; ++t) mesh_set_instances(&ship_mesh[t][0], dv.ships_il[t].v, dv.ships_il[t].n);
    mesh_set_instances(&mesh_puff, dv.puffs_il.v, dv.puffs_il.n);
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
        scene_assets_begin();
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
        scene_assets_end();
    } else {
        mesh_set_instances(&mesh_house, 0, 0); mesh_set_instances(&mesh_roof, 0, 0); mesh_set_instances(&mesh_tower, 0, 0);
        mesh_set_instances(&mesh_wall, 0, 0); mesh_set_instances(&mesh_pier, 0, 0); mesh_set_instances(&mesh_flagpole, 0, 0);
    }
    mesh_set_instances(&dv.stage, &(inst_t){ dv.cx, 0, dv.cz, 0, 1, 1,1,1, 0,0,0,0 }, 1);
}

int detailview_submit(void) {
    if (dv.phase == DVP_IDLE || dv.phase == DVP_OUT || !dv.stage_built) return 0;
    int n = 0;
    mesh_draw(&dv.stage); n++;
    for (int t = 0; t < SHIP_COUNT; ++t) { mesh_draw(&ship_mesh[t][0]); n += ship_mesh[t][0].ninst; }
    if (dv.mode == DV_CITY) {
        mesh_draw(&mesh_house); mesh_draw(&mesh_roof); mesh_draw(&mesh_tower);
        mesh_draw(&mesh_wall); mesh_draw(&mesh_pier); mesh_draw(&mesh_flagpole);
        n += mesh_house.ninst + mesh_roof.ninst + mesh_tower.ninst + mesh_wall.ninst + mesh_pier.ninst + mesh_flagpole.ninst;
        n += scene_assets_submit();                                   // church + authored houses, LOD-picked in detailview_frame
    }
    return n;
}

// The staged scene as shadow casters: the same stage and ships, but the authored buildings
// come from their coarse caster meshes (scene.c, DESIGN_VISUAL.md step 3).
int detailview_submit_shadow(void) {
    if (dv.phase == DVP_IDLE || dv.phase == DVP_OUT || !dv.stage_built) return 0;
    int n = 0;
    mesh_draw(&dv.stage); n++;
    for (int t = 0; t < SHIP_COUNT; ++t) { mesh_draw(&ship_mesh[t][0]); n += ship_mesh[t][0].ninst; }
    if (dv.mode == DV_CITY) {
        mesh_draw(&mesh_house); mesh_draw(&mesh_roof); mesh_draw(&mesh_tower);
        mesh_draw(&mesh_wall); mesh_draw(&mesh_pier); mesh_draw(&mesh_flagpole);
        n += mesh_house.ninst + mesh_roof.ninst + mesh_tower.ninst + mesh_wall.ninst + mesh_pier.ninst + mesh_flagpole.ninst;
        n += scene_assets_submit_shadow();
    }
    return n;
}

int detailview_submit_smoke(void) { mesh_draw(&mesh_puff); return mesh_puff.ninst; }

// ---------------------------------------------------------------------------
// cloud overlay: procedural fbm clouds over the finished frame (after the tonemap)

static const char *cloud_vs =
    "in vec2 a_pos; out vec2 v_uv;\n"
    "void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *cloud_fs =
    "in vec2 v_uv; uniform float u_cover, u_time, u_aspect;\n"
    "out vec4 fragColor;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }\n"
    "float noise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }\n"
    "void main(){\n"
    "  if (u_cover <= 0.001) discard;\n"
    "  vec2 p = vec2(v_uv.x * u_aspect, v_uv.y) * 3.0 + vec2(u_time * 0.05, u_time * 0.013);\n"
    "  float f = 0.0, a = 0.55; for (int i = 0; i < 5; ++i) { f += a * noise(p); p *= 2.03; a *= 0.5; }\n"
    "  float edge = 1.05 - u_cover * 1.6;\n"                       // cover grows: threshold sweeps down through the fbm
    "  float alpha = smoothstep(edge, edge + 0.38, f) * smoothstep(0.0, 0.08, u_cover);\n"
    "  vec3 col = mix(vec3(0.78, 0.82, 0.88), vec3(0.98, 0.985, 1.0), f);\n"
    "  fragColor = vec4(col, alpha);\n"
    "}\n";

void detailview_clouds(void) {
    float cover = dv_cover();
    if (cover <= 0.001f) return;
    if (!dv.cloud_init) {
        static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
        static const rhi_layout_t layout = { attrs, 1, { 2 * sizeof(float), 0 } };
        static const char *uniforms[] = { "u_cover", "u_time", "u_aspect", 0 };
        dv.cloud_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = cloud_vs, .fs = cloud_fs, .layout = &layout, .uniforms = uniforms, .blend = RHI_BLEND_ALPHA });
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
