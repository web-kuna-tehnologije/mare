// AI (see ai.h). Sections: trade planner (greedy lot fill, return-leg estimate, risk) · merchants
// (sell all, repair, remit capital, route or free trade, wander) · pirates & warships (prey
// selection by relative strength, raids on busy lanes, patrols, limited sieges) · colonies &
// treasure · per-fleet dispatcher (ai_think) · spawns · nation AI (fleet purchases, investments,
// historical and opportunistic colonisation, coast claims, pirate respawn, treasure fleet).
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ai.h"
#include "economy.h"
#include "fleet.h"
#include "combat.h"
#include "settlement.h"
#include "region.h"
#include "../world/regions.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../core/diag.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/nav.h"

// ---------------------------------------------------------------------------
// trade planner

static float risk_at_port(int b) {
    float r = 0; const port_t *p = &G.ports[b];
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *o = &G.fleets[i]; if (o->alive && o->role == ROLE_PIRATE && dist2d(o->x, o->z, p->x, p->z) < 20.f * WORLD_SCALE) r += 1; }
    return r;
}

// rough value of the best single cargo from b back to a
static float best_return_margin(const fleet_t *f, int b, int a, float cap) {
    const port_t *pb = &G.ports[b], *pa = &G.ports[a];
    float best = 0;
    for (int g = 0; g < MAX_GOODS; ++g) {
        float avail = pb->stock[g] - port_norm(pb, g) * 0.15f; if (avail < 5) continue;
        float q = minf(minf(avail, cap), 300);
        float cost = trade_cost(pb, g, q) * (1 + tariff_rate(pb, f->nation));
        float rev = trade_revenue(pa, g, q) * (1 - tariff_rate(pa, f->nation));
        best = maxf(best, rev - cost);
    }
    return best;
}

trade_plan_t ai_plan_trade(fleet_t *f, int a, const int *allowed, int nallowed, float max_dist) {
    static int p_plan = -1; if (p_plan < 0) p_plan = prof_id("ai.plan_trade");
    prof_begin(p_plan);
    trade_plan_t best = { -1, 0, -1e9f, {0} };
    const port_t *pa = &G.ports[a];
    float cap = fleet_capacity(f) - fleet_cargo(f);
    float wallet = (float)*wallet_of(f);
    float speed = BASE_SPEED * fleet_speed(f);
    static const float qopts[4] = { 300, 120, 40, 12 };
    int ncand = nallowed ? nallowed : G.nports;
    for (int bi = 0; bi < ncand; ++bi) {
        int b = nallowed ? allowed[bi] : bi;
        if (b == a) continue;
        const port_t *pb = &G.ports[b];
        if (!can_trade(pb, f->nation)) continue;
        float d = nav_port_dist(a, b); if (d < 0 || d > max_dist) continue;
        float pref = pb->nation == f->nation ? 1.15f : pb->nation == NATION_NONE ? 1.0f : 0.85f;  // mercantilism
        float room = cap, money = wallet, profit = 0, buy[MAX_GOODS] = {0};
        for (int g = 0; g < MAX_GOODS; ++g) if (f->cargo[g] > 0) profit += trade_revenue(pb, g, f->cargo[g]) * (1 - tariff_rate(pb, f->nation));
        for (int iter = 0; iter < 4 && room > 5 && money > 50; ++iter) {   // greedy fill, best absolute profit per good
            int bg = -1; float bp = 0, bq = 0;
            for (int g = 0; g < MAX_GOODS; ++g) {
                if (buy[g] > 0) continue;
                float avail = pa->stock[g] - port_norm(pa, g) * 0.15f; if (avail < 5) continue;
                for (int k = 0; k < 4; ++k) {
                    float q = minf(minf(avail, room), qopts[k]); if (q < 5) continue;
                    float cost = trade_cost(pa, g, q) * (1 + tariff_rate(pa, f->nation));
                    if (cost > money) { q *= money / cost * 0.95f; if (q < 5) continue; cost = trade_cost(pa, g, q) * (1 + tariff_rate(pa, f->nation)); }
                    float held = f->cargo[g] + buy[g];
                    float rev = (trade_revenue(pb, g, held + q) - trade_revenue(pb, g, held)) * (1 - tariff_rate(pb, f->nation));
                    float pr = rev - cost;
                    if (pr > bp) { bp = pr; bg = g; bq = q; }
                }
            }
            if (bg < 0 || bp < 20 || bp < 0.06f * trade_cost(pa, bg, bq)) break;
            float cost = trade_cost(pa, bg, bq) * (1 + tariff_rate(pa, f->nation));
            buy[bg] += bq; room -= bq; money -= cost; profit += bp;
        }
        float back = best_return_margin(f, b, a, cap);
        float days = d / maxf(speed, 1) + 3;
        float score = ((profit + back * 0.6f) / (days * 1.6f)) * pref - risk_at_port(b) * 30;
        if (score > best.score) { best.score = score; best.port = b; best.profit = profit; memcpy(best.buy, buy, sizeof buy); }
    }
    prof_end(p_plan);
    return best;
}

void ai_execute_plan(fleet_t *f, int a, const trade_plan_t *plan) {
    port_t *pa = &G.ports[a];
    for (int g = 0; g < MAX_GOODS; ++g) if (plan->buy[g] > 0) trade_buy(f, pa, g, plan->buy[g]);
    fleet_order_dock(f, plan->port);
    if (f->order != ORDER_DOCK) { LOG_DEBUG("ai", "%s: planned %s -> %s but no route", f->name, pa->name, G.ports[plan->port].name); return; }   // cargo will be resold here
    LOG_TRACE("ai", "%s: %s -> %s, expected profit %.0f (score %.1f)", f->name, pa->name, G.ports[plan->port].name, plan->profit, plan->score);
    f->order = ORDER_TRADE;
    lane_register(a, plan->port, f);
}

// ---------------------------------------------------------------------------
// merchants

static void merchant_think(fleet_t *f) {
    int a = f->at_port; if (a < 0) {
        if (f->order == ORDER_IDLE) { int h = port_nearest(f->x, f->z, -1, 1, f->nation); if (h >= 0) { fleet_order_dock(f, h); if (f->order == ORDER_DOCK) f->order = ORDER_TRADE; } }
        return;
    }
    if (f->dock_timer > 0) { f->think_timer = f->dock_timer; return; }
    port_t *pa = &G.ports[a];
    fleet_sell_all(f, pa);
    fleet_repair(f, 10);
    if (!is_player(f->nation)) {   // remit profits above working capital to the crown, or borrow when broke
        double cap = AI_MERCHANT_CAPITAL + AI_MERCHANT_CAPITAL_SHIP * f->nships + 2000;
        if (f->money > cap) { G.nations[f->nation].treasury += f->money - cap; f->money = cap; }
        else if (f->money < 2000 && G.nations[f->nation].treasury > 20000) { G.nations[f->nation].treasury -= 4000; f->money += 4000; }
    }
    if (f->autotrade && f->nroute >= 2) {   // fixed route: next port in sequence
        int idx = -1; for (int i = 0; i < f->nroute; ++i) if (f->route[i] == a) idx = i;
        int next = f->route[(idx + 1) % f->nroute]; if (next == a) next = f->route[(idx + 2) % f->nroute];
        trade_plan_t p = ai_plan_trade(f, a, &next, 1, 1e9f);
        if (p.port < 0) p.port = next;
        ai_execute_plan(f, a, &p);
        return;
    }
    float max_dist = f->nation == NATION_NONE ? AI_FREECITY_RANGE : AI_MERCHANT_RANGE;
    trade_plan_t p = ai_plan_trade(f, a, 0, 0, max_dist);
    if (p.port >= 0 && p.score > 2) { ai_execute_plan(f, a, &p); return; }
    f->think_timer = rnd_range(&G.rng, 4, 10);
    if (rnd_f(&G.rng) < 0.3f) {   // nothing worth doing: wander to another market
        int b = port_random_of(f->nation, 1); if (b < 0) b = port_random(2, -1);
        if (b >= 0 && b != a && can_trade(&G.ports[b], f->nation)) { float d = nav_port_dist(a, b); if (d > 0 && d < max_dist) { fleet_order_dock(f, b); if (f->order == ORDER_DOCK) f->order = ORDER_TRADE; } }
    }
}

// ---------------------------------------------------------------------------
// pirates & warships

static float fleet_strength(const fleet_t *f) {
    float s = 0; for (int i = 0; i < f->nships; ++i) s += ship_types[f->ships[i].type].guns * (0.5f + 0.5f*f->ships[i].hull/ship_types[f->ships[i].type].hull);
    return s * f->morale + 1;
}

static const fleet_t *find_prey(const fleet_t *f, float range, int want_soft) {
    const fleet_t *best = 0; float bs = 0, my = fleet_strength(f);
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *o = &G.fleets[i];
        if (!o->alive || o == f || o->at_port >= 0 || !at_war(f->nation, o->nation)) continue;
        float d = dist2d(f->x, f->z, o->x, o->z); if (d > range) continue;
        float their = fleet_strength(o);
        if (want_soft && their > my * 0.8f) continue;       // pirates pick soft targets
        if (!want_soft && their > my * 1.5f) continue;      // navies avoid hopeless odds
        float s = (want_soft ? fleet_cargo(o) + 20 : their + 10) / (d + 2);
        if (s > bs) { bs = s; best = o; } }
    return best;
}

static void pirate_think(fleet_t *f) {
    if (f->at_port >= 0) {
        port_t *p = &G.ports[f->at_port];
        for (int g = 0; g < MAX_GOODS; ++g) if (f->cargo[g] > 0) { p->stock[g] += f->cargo[g]; f->money += f->cargo[g] * goods[g].base_price * 0.5f; f->cargo[g] = 0; }
        while (f->nships > 2) f->money += ship_types[f->ships[--f->nships].type].cost * 0.3f;   // prizes are sold
        fleet_repair(f, 1000); f->morale = 1;
        int target = -1; float bt = 0;   // hunting ground: a busy lane end near home
        for (int i = 0; i < G.nlanes; ++i) { const lane_t *l = &G.lanes[i]; float d = dist2d(p->x, p->z, G.ports[l->a].x, G.ports[l->a].z);
            float s = l->traffic / (1 + d*0.01f) * rnd_range(&G.rng, 0.5f, 1.5f); if (s > bt) { bt = s; target = rnd_f(&G.rng) < 0.5f ? l->a : l->b; } }
        if (target < 0) target = port_random(2, -1);
        if (target >= 0) { const port_t *t = &G.ports[target]; float ang = rnd_f(&G.rng) * 6.28f, r = rnd_range(&G.rng, 6, 14);
            float tx = t->x + cosf(ang)*r, tz = t->z + sinf(ang)*r;
            if (nav_snap_reachable(&tx, &tz, 10, p->x, p->z)) { fleet_undock(f); f->dest_x = tx; f->dest_z = tz; if (fleet_set_path(f, tx, tz)) f->order = ORDER_RAID; } }
        f->think_timer = 2;
        return;
    }
    if (fleet_cargo(f) > fleet_capacity(f) * 0.6f || fleet_hull_fraction(f) < 0.5f || f->morale < 0.4f || f->nships >= PIRATE_MAX_SHIPS) {
        int h = f->home_port >= 0 && G.ports[f->home_port].size > 0 && G.ports[f->home_port].haven ? f->home_port : port_nearest(f->x, f->z, NATION_PIRATES, 1, -1);
        if (h >= 0) { fleet_order_dock(f, h); f->think_timer = 3; return; }
    }
    const fleet_t *prey = find_prey(f, 22, 1);
    if (prey && f->order != ORDER_ATTACK) { fleet_order_attack(f, prey); f->think_timer = 1; return; }
    if (f->order == ORDER_IDLE) { float ang = rnd_f(&G.rng) * 6.28f, tx = f->x + cosf(ang)*20, tz = f->z + sinf(ang)*20;
        if (nav_snap_reachable(&tx, &tz, 10, f->x, f->z) && fleet_set_path(f, tx, tz)) { f->order = ORDER_RAID; f->dest_x = tx; f->dest_z = tz; } }
    f->think_timer = rnd_range(&G.rng, 1, 2);
}

static int count_sieges(int nation) { int n = 0; for (int i = 0; i < G.nfleets; ++i) if (G.fleets[i].alive && G.fleets[i].nation == nation && G.fleets[i].order == ORDER_SIEGE) n++; return n; }

static void warship_think(fleet_t *f) {
    const fleet_t *prey = find_prey(f, SIGHT_RANGE, 0);
    if (prey && (f->order == ORDER_PATROL || f->order == ORDER_IDLE || f->order == ORDER_MOVE)) { fleet_order_attack(f, prey); f->think_timer = 1; return; }
    if (f->at_port >= 0) {
        fleet_repair(f, 15); f->morale = minf(1, f->morale + 0.2f);
        const nation_t *nt = &G.nations[f->nation];
        int best = -1; float bs = 0;
        if (count_sieges(f->nation) == 0 && nt->treasury > AI_SIEGE_TREASURY && f->nships >= 3 && fleet_guns(f) >= 90)
        for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i];
            if (p->size <= 0 || !at_war(f->nation, p->nation) || p->nation == NATION_PIRATES || p->capture_cooldown > 0) continue;
            if (nt->war_timer[p->nation] < AI_SIEGE_WAR_AGE) continue;                // no surprise sieges
            if (nt->captures_this_war[p->nation] >= AI_SIEGE_MAX_CAPTURES) continue;   // limited war aims
            if (p->size >= 3 && fleet_guns(f) < 200) continue;                          // great cities need great fleets
            float d = dist2d(f->x, f->z, p->x, p->z); if (d > 260) continue;
            float s = fleet_guns(f) / (p->fort_max + 20) / (1 + d * 0.01f) * (4 - p->size);
            if (s > bs) { bs = s; best = i; } }
        if (best >= 0 && bs > 0.25f && rnd_f(&G.rng) < AI_SIEGE_CHANCE) { fleet_order_siege(f, best, 0); f->think_timer = 5; return; }
        if (rnd_f(&G.rng) < 0.7f) { int p = port_random_of(f->nation, 1); if (p >= 0) { const port_t *pt = &G.ports[p];
            float ang = rnd_f(&G.rng)*6.28f, r = rnd_range(&G.rng, 4, 12), tx = pt->x + cosf(ang)*r, tz = pt->z + sinf(ang)*r;
            if (nav_snap_reachable(&tx, &tz, 10, pt->x, pt->z)) fleet_order_patrol(f, tx, tz); } }
        f->think_timer = rnd_range(&G.rng, 3, 8);
        return;
    }
    if (f->order == ORDER_IDLE) { int h = port_nearest(f->x, f->z, f->nation, 1, -1); if (h < 0) h = port_nearest(f->x, f->z, -1, 1, f->nation); if (h >= 0) fleet_order_dock(f, h); }
    f->think_timer = rnd_range(&G.rng, 1.5f, 3);
}

// ---------------------------------------------------------------------------
// colonies & treasure

void ai_colony_found(fleet_t *f, int port) {
    port_t *p = &G.ports[port];
    if (!port_is_site(p)) return;
    if (is_player(f->nation)) {
        if (G.nations[f->nation].treasury < COLONY_COST) { log_event_at(EV_INFO, f->nation, 1, p->x, p->z, "The settlers at %s cannot be provisioned: %.0f ducats needed", p->name, COLONY_COST); return; }
        G.nations[f->nation].treasury -= COLONY_COST;
    }
    settlement_found(p, f->nation, 1.6f);
    for (int g = 0; g < MAX_GOODS; ++g) { p->stock[g] = port_norm(p, g) * 0.5f + f->cargo[g]; f->cargo[g] = 0; }
    p->stock[G_GRAIN] += 80; p->stock[G_TIMBER] += 40;
    port_update_prices(p);
    log_event_at(EV_COLONY, f->nation, 0, p->x, p->z, "%s settlers found a colony at %s", nations_info[f->nation].adj, p->name);
    if (f->role == ROLE_COLONIST) { f->role = ROLE_TRADER; f->autotrade = !is_player(f->nation); }
}

void ai_treasure_arrived(fleet_t *f, port_t *pt) {
    float b = f->cargo[G_BULLION]; f->cargo[G_BULLION] = 0;
    G.nations[f->nation].treasury += b * goods[G_BULLION].base_price * 1.5f;
    if (is_player(f->nation)) G.nations[f->nation].income_acc += b * goods[G_BULLION].base_price * 1.5f;
    log_event_at(EV_TREASURE, f->nation, 1, pt->x, pt->z, "The %s treasure fleet reaches %s with %.0f chests of silver", nations_info[f->nation].adj, pt->name, b);
    f->role = ROLE_WARSHIP; f->order = ORDER_DOCK; f->think_timer = 10;
    snprintf(f->name, sizeof f->name, "Armada Escort %d", f->id);
}

// ---------------------------------------------------------------------------
// per-fleet dispatcher

void ai_think(fleet_t *f) {
    int player = is_player(f->nation);
    switch (f->role) {
    case ROLE_PIRATE:  pirate_think(f); break;
    case ROLE_WARSHIP: if (!player) warship_think(f); break;
    case ROLE_TRADER:
        if (f->autotrade || !player) merchant_think(f);
        else if (f->order == ORDER_TRADE && f->at_port >= 0) f->order = ORDER_DOCK;
        break;
    case ROLE_TREASURE:   // the player commands their own flota; the AI's sails home again if it had to shelter
        if (!player && f->at_port >= 0 && f->order == ORDER_DOCK && f->home_port >= 0 && f->at_port != f->home_port) { fleet_order_dock(f, f->home_port); if (f->order == ORDER_DOCK) f->order = ORDER_TRADE; }
        break;
    case ROLE_COLONIST:
        if (f->order == ORDER_IDLE && f->target_port >= 0 && port_is_site(&G.ports[f->target_port])) fleet_order_colonize(f, f->target_port);
        break;
    }
    if (!player && f->order == ORDER_FLEE && f->at_port >= 0) { f->order = ORDER_DOCK; f->think_timer = 4; }
}

// ---------------------------------------------------------------------------
// spawns

fleet_t *spawn_merchant(int nation, int port) {
    port_t *p = &G.ports[port];
    fleet_t *f = fleet_new(nation, ROLE_TRADER, p->x, p->z, 0); if (!f) return 0;
    fleet_add_ship(f, rnd_f(&G.rng) < 0.35f ? SHIP_MERCHANT : SHIP_FLUYT);
    if (rnd_f(&G.rng) < 0.4f) fleet_add_ship(f, SHIP_FLUYT);
    f->at_port = port; f->home_port = port; f->autotrade = 1; f->order = ORDER_DOCK; f->think_timer = rnd_range(&G.rng, 1, 5);
    f->money = AI_MERCHANT_CAPITAL + AI_MERCHANT_CAPITAL_SHIP * f->nships;
    G.nations[nation].treasury -= fleet_value(f) + f->money;
    return f;
}
fleet_t *spawn_warfleet(int nation, int port, int strength) {
    port_t *p = &G.ports[port];
    fleet_t *f = fleet_new(nation, ROLE_WARSHIP, p->x, p->z, 0); if (!f) return 0;
    for (int i = 0; i < strength; ++i) { float r = rnd_f(&G.rng); fleet_add_ship(f, r < 0.3f ? SHIP_GALLEON : r < 0.5f ? SHIP_LINER : SHIP_FRIGATE); }
    f->at_port = port; f->home_port = port; f->order = ORDER_DOCK; f->think_timer = rnd_range(&G.rng, 2, 6);
    G.nations[nation].treasury -= fleet_value(f);
    return f;
}
fleet_t *spawn_pirates(int haven, int n) {
    port_t *p = &G.ports[haven];
    fleet_t *f = fleet_new(NATION_PIRATES, ROLE_PIRATE, p->x, p->z, 0); if (!f) return 0;
    for (int i = 0; i < n; ++i) fleet_add_ship(f, rnd_f(&G.rng) < 0.7f ? SHIP_SLOOP : SHIP_FRIGATE);
    f->at_port = haven; f->home_port = haven; f->order = ORDER_DOCK; f->money = 2000; f->think_timer = rnd_range(&G.rng, 1, 4);
    return f;
}
void spawn_treasure_fleet(void) {
    static const char *sources[] = { "Veracruz", "Cartagena", "Portobelo" };
    int cand[3], n = 0;
    for (int k = 0; k < 3; ++k) { int i = port_by_name(sources[k]); if (i >= 0 && G.ports[i].nation == NATION_SPAIN && G.ports[i].size > 0) cand[n++] = i; }
    int dest = port_by_name("Seville");
    if (!n || dest < 0 || G.ports[dest].nation != NATION_SPAIN) return;
    int a = cand[rnd_int(&G.rng, n)]; port_t *p = &G.ports[a];
    fleet_t *f = fleet_new(NATION_SPAIN, ROLE_TREASURE, p->x, p->z, "Flota de Indias"); if (!f) return;
    int ng = 2 + rnd_int(&G.rng, 3); for (int i = 0; i < ng; ++i) fleet_add_ship(f, SHIP_GALLEON); fleet_add_ship(f, SHIP_LINER);
    float taken = minf(p->stock[G_BULLION], 300); p->stock[G_BULLION] -= taken;
    f->cargo[G_BULLION] = rnd_range(&G.rng, 500, 900) + taken; f->cargo[G_SUGAR] = 120; f->cargo[G_TOBACCO] = 80;
    f->home_port = dest;
    fleet_order_dock(f, dest); if (f->order == ORDER_DOCK) f->order = ORDER_TRADE;
    log_event_at(EV_TREASURE, NATION_SPAIN, 1, p->x, p->z, "The Spanish treasure fleet sails from %s for Seville laden with silver%s", p->name, is_player(NATION_SPAIN) ? " - it is under your command" : "");
}

// ---------------------------------------------------------------------------
// nation AI

static void nation_invest(int n) {
    nation_t *nt = &G.nations[n];
    int pi = port_random_of(n, 1); if (pi < 0) return;
    port_t *pt = &G.ports[pi];
    int b = pt->building[B_DOCKS] == 0 && pt->size >= 2 ? B_DOCKS
          : pt->building[B_FORT] < 1 + (pt->size >= 2) ? B_FORT
          : port_colonial(pt) && (pt->produce[G_SUGAR] > 0 || pt->produce[G_TOBACCO] > 0) && pt->building[B_PLANTATION] < 2 ? B_PLANTATION
          : pt->produce[G_BULLION] > 0 && pt->building[B_MINE] < 2 ? B_MINE
          : pt->building[B_WAREHOUSE] < 2 ? B_WAREHOUSE
          : pt->building[B_MARKET] < 2 ? B_MARKET : -1;
    if (b < 0 || pt->building[b] >= 3) return;
    nt->treasury -= buildings[b].cost * (1 + pt->building[b]); pt->building[b]++; territory_dirty = 1;
    if (b == B_FORT || b == B_DOCKS) log_event_at(EV_INFO, n, 0, pt->x, pt->z, "%s builds %s in %s", nations_info[n].name, buildings[b].name, pt->name);
}

static void nation_claim_coast(int n) {   // AI free colonisation: a coast near one of its colonies
    nation_t *nt = &G.nations[n];
    int from = port_random_of(n, 1); if (from < 0) return;
    const port_t *pf = &G.ports[from]; if (!port_colonial(pf)) return;
    float wx = 0, wz = 0, best = 0; int found = 0; char reason[96];
    for (int k = 0; k < 10; ++k) {   // sample coasts around the colony and keep the most attractive region (directed colonisation)
        float ang = rnd_f(&G.rng) * 6.2832f, r = rnd_range(&G.rng, MIN_SITE_DIST + 1, 14), cx = pf->x + cosf(ang) * r, cz = pf->z + sinf(ang) * r;
        if (!world_is_land(cx, cz) || !site_check(cx, cz, n, reason, sizeof reason)) continue;
        float a = region_attractiveness(region_at(cx, cz)) + 0.1f; if (a > best) { best = a; wx = cx; wz = cz; found = 1; }
    }
    if (!found) { LOG_TRACE("ai", "claim near %s: no suitable coast", pf->name); return; }
    float pot[MAX_GOODS]; site_potential(x2lon(wx), z2lat(wz), pot); float sum = 0; for (int g = 0; g < MAX_GOODS; ++g) sum += pot[g];
    if (sum < 2.0f) { LOG_TRACE("ai", "claim near %s: poor coast (%.1f)", pf->name, sum); return; }   // only worthwhile coasts
    int s = site_create(wx, wz, n); if (s < 0) return;
    fleet_t *f = fleet_new(n, ROLE_COLONIST, pf->x, pf->z, 0); if (!f) return;
    fleet_add_ship(f, SHIP_FLUYT); f->cargo[G_GRAIN] = 100; f->cargo[G_TIMBER] = 60; f->cargo[G_ARMS] = 20;
    nt->treasury -= 9000; fleet_order_colonize(f, s);
    if (f->order != ORDER_COLONIZE) { fleet_destroy(f); return; }
    log_event_at(EV_COLONY, n, 0, pf->x, pf->z, "%s settlers leave %s to claim a new coast at %s", nations_info[n].adj, pf->name, G.ports[s].name);
}

static void nation_colonize(int n, int year) {
    nation_t *nt = &G.nations[n];
    if (year >= 1628 && nt->treasury > 60000 && nt->ports >= 2 && rnd_f(&G.rng) < 0.12f) nation_claim_coast(n);
    for (int i = 0; i < G.nports; ++i) { port_t *s = &G.ports[i]; if (!port_is_site(s)) continue;
        int historical = s->preferred_nation == n && year >= s->settle_year - 2;
        int opportunist = year >= 1640 && s->preferred_nation != n && rnd_f(&G.rng) < 0.02f && nt->treasury > 60000;
        if (!(historical || opportunist) || nt->treasury < 15000) continue;
        int claimed = 0; for (int k = 0; k < G.nfleets; ++k) if (G.fleets[k].alive && G.fleets[k].order == ORDER_COLONIZE && G.fleets[k].target_port == i) claimed = 1;
        if (claimed) continue;
        int from = port_nearest(s->x, s->z, n, 1, -1); if (from < 0) continue;
        fleet_t *f = fleet_new(n, ROLE_COLONIST, G.ports[from].x, G.ports[from].z, 0); if (!f) continue;
        fleet_add_ship(f, SHIP_FLUYT); f->cargo[G_GRAIN] = 100; f->cargo[G_TIMBER] = 60; f->cargo[G_ARMS] = 20;
        nt->treasury -= 9000; fleet_order_colonize(f, i);
        log_event_at(EV_COLONY, n, 0, G.ports[from].x, G.ports[from].z, "%s settlers sail from %s to found %s", nations_info[n].adj, G.ports[from].name, s->name);
        break;
    }
}

void ai_nations_tick(float dt) {
    int y, m, d; day2date(G.day, &y, &m, &d);
    G.treasure_timer += dt;
    if (m == 4 && G.treasure_timer > 300) { G.treasure_timer = 0; spawn_treasure_fleet(); }

    for (int n = NATION_NONE; n < MAX_NATIONS; ++n) {
        nation_t *nt = &G.nations[n];
        nt->ai_timer -= dt; if (nt->ai_timer > 0) continue;
        nt->ai_timer = rnd_range(&G.rng, 12, 25);
        if (n == NATION_PIRATES) {
            int pirates = 0, cand[MAX_PORTS], havens = 0;
            for (int i = 0; i < G.nfleets; ++i) if (G.fleets[i].alive && G.fleets[i].role == ROLE_PIRATE) pirates++;
            for (int i = 0; i < G.nports; ++i) if (G.ports[i].size > 0 && (G.ports[i].haven || G.ports[i].nation == NATION_PIRATES)) cand[havens++] = i;
            if (havens && pirates < 1 + havens) spawn_pirates(cand[rnd_int(&G.rng, havens)], 1 + rnd_int(&G.rng, 2));
            continue;
        }
        if (is_player(n)) continue;
        int merchants = 0, warships = 0;
        for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i]; if (!f->alive || f->nation != n) continue; merchants += f->role == ROLE_TRADER; warships += f->role == ROLE_WARSHIP; }
        int want_m = nations_info[n].ai_merchants + nt->ports / 2, want_w = nations_info[n].ai_warships + nt->ports / 8;
        int home = port_random_of(n, 2); if (home < 0) home = port_random_of(n, 1);
        if (home >= 0 && merchants < want_m && nt->treasury > AI_BUY_MERCHANT_TREASURY) spawn_merchant(n, home);
        if (home >= 0 && warships < want_w && nt->treasury > AI_BUY_WARSHIP_TREASURY) spawn_warfleet(n, nt->capital >= 0 && G.ports[nt->capital].nation == n ? nt->capital : home, 2 + rnd_int(&G.rng, 2));
        if (nt->treasury > AI_INVEST_TREASURY) nation_invest(n);
        nation_colonize(n, y);
    }
}
