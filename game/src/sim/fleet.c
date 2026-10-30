// Fleets (see fleet.h). Sections: lifecycle · orders (every order computes its path first and
// leaves the fleet IDLE if unreachable) · trade lanes · arrival handling (fleet_arrived resolves
// the order: dock/trade/flee/colonize/patrol bounce) · movement (wind factor, storm damage,
// heading smoothing, waypoint advance) · per-fleet tick (morale, upkeep, AI think, move, siege).
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fleet.h"
#include "economy.h"
#include "events.h"
#include "ai.h"
#include "combat.h"
#include "settlement.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../data/tables.h"
#include "../world/nav.h"
#include "../world/wind.h"

// ---------------------------------------------------------------------------
// lifecycle

fleet_t *fleet_new(int nation, int role, float x, float z, const char *name) {
    int slot = -1;
    for (int i = 0; i < MAX_FLEETS; ++i) if (!G.fleets[i].alive) { slot = i; break; }
    if (slot < 0) { LOG_WARN("fleet", "fleet table full (%d): cannot spawn for nation %d", MAX_FLEETS, nation); return 0; }
    fleet_t *f = &G.fleets[slot];
    memset(f, 0, sizeof *f);
    f->alive = 1; f->id = ++G.next_fleet_id; f->nation = nation; f->role = role; f->x = x; f->z = z;
    f->morale = 1; f->at_port = -1; f->target_port = -1; f->home_port = -1;
    f->think_timer = rnd_range(&G.rng, 0.5f, 3.f);
    if (name) snprintf(f->name, sizeof f->name, "%s", name);
    else snprintf(f->name, sizeof f->name, "%s %s %d", nations_info[nation].adj,
                  role == ROLE_TRADER ? "Convoy" : role == ROLE_WARSHIP ? "Squadron" : role == ROLE_PIRATE ? "Raiders" : role == ROLE_TREASURE ? "Flota" : "Settlers", f->id);
    if (slot + 1 > G.nfleets) G.nfleets = slot + 1;
    G.nations[nation].fleets++;
    return f;
}
void fleet_add_ship(fleet_t *f, int type) {
    if (f->nships >= MAX_SHIPS) return;
    f->ships[f->nships].type = type; f->ships[f->nships].hull = ship_types[type].hull; f->nships++;
}
void fleet_destroy(fleet_t *f) {
    if (!f->alive) return;
    f->alive = 0; G.nations[f->nation].fleets--;
    for (int i = 0; i < G.nfleets; ++i) { fleet_t *o = &G.fleets[i]; if (!o->alive) continue;
        if (o->target_fleet_id == f->id) { o->target_fleet_id = 0; if (o->order == ORDER_ATTACK) o->order = ORDER_IDLE; }
        if (o->flee_from_id == f->id) o->flee_from_id = 0; }
}
fleet_t *fleet_by_id(int id) {
    if (id <= 0) return 0;
    for (int i = 0; i < G.nfleets; ++i) if (G.fleets[i].alive && G.fleets[i].id == id) return &G.fleets[i];
    return 0;
}
int fleet_index(const fleet_t *f) { return (int)(f - G.fleets); }

// ---------------------------------------------------------------------------
// orders

// returns 0 and cancels the order when the destination is unreachable (never sails over land)
int fleet_set_path(fleet_t *f, float tx, float tz) {
    float len = nav_path(f->x, f->z, tx, tz, f->path_x, f->path_z, &f->npath, MAX_WAYPOINTS);
    f->pathi = 0;
    if (len < 0 || f->npath == 0) { f->npath = 0; f->order = ORDER_IDLE; f->target_port = -1; f->target_fleet_id = 0; LOG_DEBUG("fleet", "%s: no route to %.1f,%.1f", f->name, x2lon(tx), z2lat(tz)); return 0; }
    return 1;
}
void fleet_undock(fleet_t *f) { f->at_port = -1; }
void fleet_expel(fleet_t *f) {
    if (f->at_port < 0) return;
    const port_t *p = &G.ports[f->at_port];
    float ang = atan2f(p->z - p->lz, p->x - p->lx);          // out to sea
    f->x = p->x + cosf(ang) * 1.25f * WORLD_SCALE; f->z = p->z + sinf(ang) * 1.25f * WORLD_SCALE; nav_snap(&f->x, &f->z, 6);
    f->at_port = -1; f->order = ORDER_IDLE; f->npath = 0; f->think_timer = 0.5f;
}
static void clear_targets(fleet_t *f) { f->target_port = -1; f->target_fleet_id = 0; }
void fleet_order_move(fleet_t *f, float x, float z)   { fleet_undock(f); clear_targets(f); f->dest_x = x; f->dest_z = z; if (fleet_set_path(f, x, z)) f->order = ORDER_MOVE; }
void fleet_order_dock(fleet_t *f, int port) {
    if (f->at_port == port) { f->order = ORDER_DOCK; return; }
    fleet_undock(f); clear_targets(f);
    if (fleet_set_path(f, G.ports[port].x, G.ports[port].z)) { f->order = ORDER_DOCK; f->target_port = port; }
}
void fleet_order_attack(fleet_t *f, const fleet_t *t) { fleet_undock(f); clear_targets(f); f->repath_timer = 1; if (fleet_set_path(f, t->x, t->z)) { f->order = ORDER_ATTACK; f->target_fleet_id = t->id; } }
void fleet_order_siege(fleet_t *f, int port, int blockade_only) { fleet_undock(f); clear_targets(f); if (fleet_set_path(f, G.ports[port].x, G.ports[port].z)) { f->order = blockade_only ? ORDER_BLOCKADE : ORDER_SIEGE; f->target_port = port; } }
void fleet_order_patrol(fleet_t *f, float x, float z) { fleet_undock(f); clear_targets(f); f->dest_x = x; f->dest_z = z; if (fleet_set_path(f, x, z)) f->order = ORDER_PATROL; }
void fleet_order_colonize(fleet_t *f, int port) { fleet_undock(f); clear_targets(f); if (fleet_set_path(f, G.ports[port].x, G.ports[port].z)) { f->order = ORDER_COLONIZE; f->target_port = port; } }
void fleet_order_stop(fleet_t *f) { f->order = ORDER_IDLE; f->npath = 0; f->autotrade = 0; f->nroute = 0; clear_targets(f); }

// ---------------------------------------------------------------------------
// lanes

void lane_register(int a, int b, const fleet_t *f) {
    if (a < 0 || b < 0 || a == b) return;
    if (a > b) { int t = a; a = b; b = t; }
    lane_t *l = 0;
    for (int i = 0; i < G.nlanes; ++i) if (G.lanes[i].a == a && G.lanes[i].b == b) { l = &G.lanes[i]; break; }
    if (!l) {
        if (G.nlanes < MAX_LANES) l = &G.lanes[G.nlanes++];
        else { int w = 0; for (int i = 1; i < G.nlanes; ++i) if (G.lanes[i].traffic < G.lanes[w].traffic) w = i; l = &G.lanes[w]; }
        memset(l, 0, sizeof *l); l->a = a; l->b = b;
        int n = mini(f->npath, LANE_PTS-1);
        l->x[0] = f->x; l->z[0] = f->z; l->npts = 1;
        for (int i = 0; i < n; ++i) { int k = (int)((long long)i * f->npath / n); l->x[l->npts] = f->path_x[k]; l->z[l->npts] = f->path_z[k]; l->npts++; }
    }
    l->traffic += 1.f;
}
void lanes_decay(float dt) { for (int i = 0; i < G.nlanes; ++i) G.lanes[i].traffic *= powf(0.985f, dt); }

// ---------------------------------------------------------------------------
// arrival

static void fleet_arrived(fleet_t *f) {
    switch (f->order) {
    case ORDER_MOVE: case ORDER_RAID: f->order = ORDER_IDLE; break;
    case ORDER_PATROL: {
        float ang = rnd_f(&G.rng) * 6.28f, r = rnd_range(&G.rng, 2.f * WORLD_SCALE, 6.f * WORLD_SCALE), tx = f->dest_x + cosf(ang)*r, tz = f->dest_z + sinf(ang)*r;
        if (!nav_snap_reachable(&tx, &tz, 10, f->x, f->z) || !fleet_set_path(f, tx, tz)) f->order = ORDER_IDLE;
    } break;
    case ORDER_DOCK: case ORDER_TRADE: case ORDER_FLEE: {
        int p = f->target_port;
        if (p < 0) { f->order = ORDER_IDLE; break; }
        port_t *pt = &G.ports[p];
        if (pt->size <= 0 || !can_trade(pt, f->nation)) {   // destination closed (war, blockade, gone): divert to the nearest friendly port
            int alt = port_nearest(f->x, f->z, -1, 1, f->nation);
            log_event_at(EV_INFO, f->nation, is_player(f->nation), f->x, f->z, "%s cannot enter %s (%s)%s%s", f->name, pt->name,
                         pt->size <= 0 ? "it no longer exists" : at_war(f->nation, pt->nation) ? "at war" : "blockaded", alt >= 0 ? " and diverts to " : "", alt >= 0 ? G.ports[alt].name : "");
            f->order = ORDER_IDLE; f->think_timer = 1;
            if (alt >= 0 && alt != p) { fleet_order_dock(f, alt); if (f->order == ORDER_DOCK && f->autotrade) f->order = ORDER_TRADE; }
            break;
        }
        f->at_port = p; f->x = pt->x; f->z = pt->z; pt->traffic += 1;
        f->dock_timer = 2.5f - 0.5f * pt->building[B_DOCKS];
        if (f->role == ROLE_TREASURE && pt->nation == f->nation && !port_colonial(pt)) { ai_treasure_arrived(f, pt); break; }
        if (f->order == ORDER_TRADE && (f->autotrade || !is_player(f->nation))) fleet_sell_all(f, pt);
        if (!is_player(f->nation) || f->autotrade) f->think_timer = f->dock_timer;
        f->order = ORDER_DOCK;
    } break;
    case ORDER_COLONIZE: {
        int p = f->target_port;
        if (p >= 0 && port_is_site(&G.ports[p])) ai_colony_found(f, p);
        f->order = ORDER_IDLE; f->think_timer = 3;
        if (p >= 0 && G.ports[p].size > 0 && can_trade(&G.ports[p], f->nation)) { f->at_port = p; f->x = G.ports[p].x; f->z = G.ports[p].z; }
    } break;
    case ORDER_SIEGE: case ORDER_BLOCKADE: case ORDER_ATTACK: break;   // handled by combat/siege ticks
    default: f->order = ORDER_IDLE;
    }
}

// ---------------------------------------------------------------------------
// movement
//
// The wind factor and effective speed (the expensive trig of the movement phase) are computed
// for every sailing fleet in parallel before the sequential tick (DESIGN_STREAMING.md task 5).
// The cache is keyed on exactly the inputs the sequential code would use - position, waypoint
// indices - so a fleet whose AI re-pathed between the pre-pass and its move simply recomputes
// inline, and the result is bit-identical either way.

typedef struct fleet_speed_cache_t { float x, z, speed; int pathi, npath; unsigned char valid; } fleet_speed_cache_t;
static fleet_speed_cache_t fleet_speed_cache[MAX_FLEETS];

static float fleet_sail_speed(const fleet_t *f, float dx, float dz) {   // dx,dz: unit direction of travel
    float wdx, wdz, wstr; wind_at(f->x, f->z, G.day, &wdx, &wdz, &wstr);
    return BASE_SPEED * fleet_speed(f) * wind_factor(dx, dz, wdx, wdz, wstr);
}

static void fleet_speed_range(void *arg, int begin, int end, int slot) {
    (void)arg; (void)slot;
    for (int i = begin; i < end; ++i) {
        const fleet_t *f = &G.fleets[i]; fleet_speed_cache_t *c = &fleet_speed_cache[i];
        c->valid = 0;
        if (!f->alive || f->at_port >= 0 || f->order == ORDER_IDLE || f->npath == 0 || f->pathi >= f->npath) continue;
        float dx = f->path_x[f->pathi] - f->x, dz = f->path_z[f->pathi] - f->z, d = sqrtf(dx*dx + dz*dz);
        if (d < 1e-4f) continue;
        c->speed = fleet_sail_speed(f, dx / d, dz / d);
        c->x = f->x; c->z = f->z; c->pathi = f->pathi; c->npath = f->npath; c->valid = 1;
    }
}

void fleets_precompute_speeds(void) {
    if (parsim_go(G.nfleets)) jobs_parallel_for(JOB_LANE_WORKER, fleet_speed_range, 0, G.nfleets, 16);
    else fleet_speed_range(0, 0, G.nfleets, 0);
}

static void fleet_move(fleet_t *f, float dt) {
    if (f->at_port >= 0) return;
    if (f->order == ORDER_ATTACK) {
        fleet_t *t = fleet_by_id(f->target_fleet_id);
        if (!t || t->at_port >= 0) { f->order = ORDER_IDLE; f->target_fleet_id = 0; return; }   // gone or escaped into harbour
        if (dist2d(f->x, f->z, t->x, t->z) < ENGAGE_RANGE * 0.8f) return;                   // in range: hold position
        f->repath_timer -= dt;
        if (f->repath_timer <= 0 || f->npath == 0) { fleet_set_path(f, t->x, t->z); f->repath_timer = 1.0f; }
    }
    if (f->order == ORDER_IDLE || f->npath == 0 || f->pathi >= f->npath) return;
    float tx = f->path_x[f->pathi], tz = f->path_z[f->pathi];
    float dx = tx - f->x, dz = tz - f->z, d = sqrtf(dx*dx + dz*dz);
    if (d < 1e-4f) { if (++f->pathi >= f->npath) fleet_arrived(f); return; }
    dx /= d; dz /= d;
    const fleet_speed_cache_t *c = &fleet_speed_cache[fleet_index(f)];
    float speed = c->valid && c->x == f->x && c->z == f->z && c->pathi == f->pathi && c->npath == f->npath
                ? c->speed                                    // the pre-pass saw exactly this state
                : fleet_sail_speed(f, dx, dz);                // re-pathed since: same math, inline
    if (f->order == ORDER_FLEE) speed *= FLEE_SPEED_MUL;
    const event_t *storm = storm_at(f->x, f->z);
    if (storm) {
        speed *= STORM_SPEED_MUL;
        for (int i = 0; i < f->nships; ++i) f->ships[i].hull -= dt * STORM_DAMAGE_DAY * storm->strength * rnd_range(&G.rng, 0.5f, 1.5f);
        for (int i = 0; i < f->nships; ) {
            if (f->ships[i].hull <= 0) { log_event_at(EV_STORM, f->nation, 0, f->x, f->z, "%s: a %s founders in the storm", f->name, ship_types[f->ships[i].type].name); G.nations[f->nation].ships_lost++; f->ships[i] = f->ships[--f->nships]; }
            else ++i;
        }
        if (f->nships == 0) { log_event_at(EV_STORM, f->nation, 0, f->x, f->z, "%s is lost with all hands", f->name); fleet_destroy(f); return; }
        fleet_jettison(f);
    }
    float step = speed * dt;
    f->heading += wrap_angle(atan2f(-dz, dx) - f->heading) * minf(1, dt * 6);
    if (step >= d) { f->x = tx; f->z = tz; if (++f->pathi >= f->npath) fleet_arrived(f); }
    else { f->x += dx * step; f->z += dz * step; }
}

// ---------------------------------------------------------------------------

void fleet_tick(fleet_t *f, float dt) {
    if (!f->alive) return;
    f->morale = minf(1.f, f->morale + MORALE_RECOVERY * dt);
    if (f->at_port >= 0 && f->dock_timer > 0) f->dock_timer -= dt;
    if (f->nation != NATION_PIRATES) {
        double up = fleet_upkeep_day(f) * dt;
        G.nations[f->nation].treasury -= up;
        if (is_player(f->nation)) G.nations[f->nation].expense_acc += up;
    }
    f->think_timer -= dt;
    if (f->think_timer <= 0) { f->think_timer = 1; ai_think(f); }
    fleet_move(f, dt);
    if (f->alive) siege_tick(f, dt);
}
