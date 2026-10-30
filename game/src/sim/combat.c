// Combat (see combat.h). fleet_take_damage is the single place where ships are lost: it handles
// cargo spill/loot, capture, bounties, morale, logging and destruction. combat_tick is O(n^2) over
// alive fleets at sea (n ~ 150) and cheap; siege_tick runs per besieging fleet.
#include <stdio.h>
#include <math.h>
#include "combat.h"
#include "economy.h"
#include "fleet.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../core/diag.h"
#include "../data/tables.h"

static float fleet_power(const fleet_t *f) {
    float s = 0; for (int i = 0; i < f->nships; ++i) s += ship_types[f->ships[i].type].guns * (0.5f + 0.5f*f->ships[i].hull/ship_types[f->ships[i].type].hull);
    return s * f->morale + 1;
}

void fleet_take_damage(fleet_t *victim, fleet_t *attacker, float dmg) {
    if (victim->nships == 0) return;
    int i = rnd_int(&G.rng, victim->nships);
    victim->ships[i].hull -= dmg;
    if (victim->ships[i].hull > 0) return;
    int type = victim->ships[i].type;
    float share = ship_types[type].cargo / maxf(fleet_capacity(victim), 1);
    int captured = attacker && (attacker->role == ROLE_PIRATE || attacker->role == ROLE_WARSHIP) && attacker->nships < MAX_SHIPS && rnd_f(&G.rng) < CAPTURE_CHANCE;
    for (int g = 0; g < MAX_GOODS; ++g) {
        float q = victim->cargo[g] * share; victim->cargo[g] -= q;
        if (attacker) { float room = maxf(fleet_capacity(attacker) - fleet_cargo(attacker), 0); attacker->cargo[g] += minf(q, room); }
    }
    if (captured) { attacker->ships[attacker->nships].type = type; attacker->ships[attacker->nships].hull = ship_types[type].hull * 0.35f; attacker->nships++; }
    if (attacker) {
        G.nations[attacker->nation].ships_sunk++;
        if (victim->role == ROLE_PIRATE && attacker->nation != NATION_PIRATES) { G.nations[attacker->nation].treasury += PIRATE_BOUNTY; if (is_player(attacker->nation)) G.nations[attacker->nation].income_acc += PIRATE_BOUNTY; }
    }
    G.nations[victim->nation].ships_lost++;
    victim->ships[i] = victim->ships[--victim->nships];
    victim->morale -= MORALE_LOSS_SHIP;
    int involved = log_player_involved(victim->nation, attacker ? attacker->nation : -1);
    if (attacker) log_event_at(EV_BATTLE, victim->nation, involved, victim->x, victim->z, "%s %s a %s of %s", attacker->name, captured ? "captures" : "sinks", ship_types[type].name, victim->name);
    if (victim->nships == 0) { log_event_at(EV_BATTLE, victim->nation, involved, victim->x, victim->z, "%s has been destroyed", victim->name); G.battles++; fleet_destroy(victim); }
}

static void flee_to_port(fleet_t *f, const fleet_t *from) {
    int h = port_nearest(f->x, f->z, -1, 1, f->nation);
    if (h < 0) return;
    fleet_order_dock(f, h); if (f->order == ORDER_DOCK) { f->order = ORDER_FLEE; f->flee_from_id = from ? from->id : 0; }
}
static int controls_itself(const fleet_t *f) { return !is_player(f->nation) || f->autotrade; }

void combat_tick(float dt) {
    for (int i = 0; i < G.nfleets; ++i) {
        fleet_t *a = &G.fleets[i]; if (!a->alive || a->at_port >= 0) continue;
        for (int j = i+1; j < G.nfleets; ++j) {
            fleet_t *b = &G.fleets[j]; if (!b->alive || b->at_port >= 0 || !at_war(a->nation, b->nation)) continue;
            float d = dist2d(a->x, a->z, b->x, b->z);
            if (d > SIGHT_RANGE) continue;
            int a_aggr = (a->order == ORDER_ATTACK && a->target_fleet_id == b->id) || a->role == ROLE_PIRATE || a->role == ROLE_WARSHIP;
            int b_aggr = (b->order == ORDER_ATTACK && b->target_fleet_id == a->id) || b->role == ROLE_PIRATE || b->role == ROLE_WARSHIP;
            if (d > ENGAGE_RANGE) {   // sighted: merchants run from stronger armed fleets
                if (controls_itself(a) && a->role == ROLE_TRADER && b_aggr && fleet_power(b) > fleet_power(a) && a->order != ORDER_FLEE) flee_to_port(a, b);
                if (controls_itself(b) && b->role == ROLE_TRADER && a_aggr && fleet_power(a) > fleet_power(b) && b->order != ORDER_FLEE) flee_to_port(b, a);
                continue;
            }
            if (!a_aggr && !b_aggr) continue;
            if (a->last_fire < G.day - 2 && b->last_fire < G.day - 2) {
                int np = port_nearest(a->x, a->z, -1, 1, -1);
                log_event_at(EV_BATTLE, -1, log_player_involved(a->nation, b->nation), a->x, a->z, "Battle off %s: %s engages %s", np >= 0 ? G.ports[np].name : "the coast", a_aggr ? a->name : b->name, a_aggr ? b->name : a->name);
            }
            a->last_fire = b->last_fire = G.day;
            float da = fleet_guns(a) * DAMAGE_PER_GUN * dt * rnd_range(&G.rng, 0.6f, 1.4f) * clampf(a->morale, 0.3f, 1.2f);
            float db = fleet_guns(b) * DAMAGE_PER_GUN * dt * rnd_range(&G.rng, 0.6f, 1.4f) * clampf(b->morale, 0.3f, 1.2f);
            fleet_take_damage(b, a, da);
            if (a->alive && b->alive) fleet_take_damage(a, b, db);
            if (!a->alive) break;
            if (a->morale < MORALE_FLEE && a->order != ORDER_FLEE && controls_itself(a)) flee_to_port(a, b->alive ? b : 0);
            if (b->alive && b->morale < MORALE_FLEE && b->order != ORDER_FLEE && controls_itself(b)) flee_to_port(b, a);
        }
    }
}

void siege_tick(fleet_t *f, float dt) {
    if (f->order != ORDER_SIEGE && f->order != ORDER_BLOCKADE) return;
    int pi = f->target_port; if (pi < 0) { f->order = ORDER_IDLE; return; }
    port_t *p = &G.ports[pi];
    if (p->nation == f->nation || p->size <= 0) { f->order = ORDER_IDLE; return; }
    if (dist2d(f->x, f->z, p->x, p->z) > 1.75f * WORLD_SCALE) return;     // still approaching (~0.44 degrees)
    f->pathi = f->npath;                                    // hold station
    p->blockaded_by = f->nation; p->blockade_timer = 1.0f;
    if (f->order != ORDER_SIEGE) return;
    if (fleet_hull_fraction(f) < 0.45f && !is_player(f->nation)) {
        int h = port_nearest(f->x, f->z, f->nation, 1, -1);
        if (h >= 0) { fleet_order_dock(f, h); if (f->order == ORDER_DOCK) f->order = ORDER_FLEE; log_event_at(EV_BATTLE, f->nation, log_player_involved(f->nation, p->nation), p->x, p->z, "%s breaks off the siege of %s", f->name, p->name); return; }
    }
    p->fort_hp -= fleet_guns(f) * SIEGE_RATE * dt * rnd_range(&G.rng, 0.7f, 1.3f);
    float fortguns = 8 + 14 * p->building[B_FORT] + 4 * p->size;
    fleet_take_damage(f, 0, fortguns * FORT_GUN_DAMAGE * dt * rnd_range(&G.rng, 0.6f, 1.4f) * clampf(p->fort_hp / p->fort_max + 0.7f, 0.4f, 1.f));
    if (!f->alive || p->fort_hp > 0) return;
    p->fort_hp = 0;
    if (fleet_crew(f) < STORM_CREW_MIN) return;
    int old = p->nation;
    G.nations[f->nation].captures_this_war[old]++;
    port_set_owner(p, f->nation);
    p->fort_hp = p->fort_max * 0.2f; p->unrest = 0.3f; p->blockaded_by = 0; p->capture_cooldown = CAPTURE_COOLDOWN;
    for (int g = 0; g < MAX_GOODS; ++g) p->stock[g] *= 0.6f;
    G.captures++;
    LOG_INFO("combat", "%s captured %s from nation %d for nation %d", f->name, p->name, old, f->nation);
    log_event_at(EV_CAPTURE, f->nation, log_player_involved(f->nation, old), p->x, p->z, "%s storms %s! The port passes from %s to %s hands", f->name, p->name, old == NATION_NONE ? "independent" : nations_info[old].adj, nations_info[f->nation].adj);
    f->order = ORDER_DOCK; f->at_port = pi; f->x = p->x; f->z = p->z; f->think_timer = 10;
}
