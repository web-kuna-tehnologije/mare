// Events and diplomacy (see events.h). Sections: event list (fixed capacity, oldest dropped) ·
// storms · diplomacy (set_war is the only place relations change; it expels docked fleets) ·
// scripted history table (year, month, a, b, war?) consumed through G.history_cursor · random
// events (each guarded by rnd_chance with a P_* rate from balance.h) · war timers and peace.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "events.h"
#include "economy.h"
#include "fleet.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../data/tables.h"

// ---------------------------------------------------------------------------
// event list

event_t *event_add(int type, float duration) {
    if (G.nevents >= MAX_EVENTS) { memmove(G.events, G.events+1, sizeof(event_t)*(MAX_EVENTS-1)); G.nevents--; }
    event_t *e = &G.events[G.nevents++];
    memset(e, 0, sizeof *e);
    e->type = type; e->day_start = G.day; e->day_end = G.day + duration; e->port = -1; e->nation = -1; e->good = -1;
    return e;
}
static void event_remove(int i) { memmove(G.events+i, G.events+i+1, sizeof(event_t)*(G.nevents-i-1)); G.nevents--; }

const event_t *storm_at(float x, float z) {
    for (int i = 0; i < G.nevents; ++i) { const event_t *e = &G.events[i]; if (e->type == EV_STORM && dist2d(x, z, e->x, e->z) < e->radius) return e; }
    return 0;
}

void event_spawn_storm(int region_hint) {
    event_t *e = event_add(EV_STORM, rnd_range(&G.rng, 6, 14));
    int month = day2month(G.day);
    int carib = region_hint == REG_CARIB || (region_hint < 0 && month >= 7 && month <= 10 && rnd_f(&G.rng) < 0.5f);
    float lon, lat;
    if (carib)                   { lon = rnd_range(&G.rng, -75, -45); lat = rnd_range(&G.rng, 12, 22);  e->vx = -1.1f; e->vz = -0.45f; e->strength = 1.4f; }
    else if (rnd_f(&G.rng) < .5f){ lon = rnd_range(&G.rng, -60, -10); lat = rnd_range(&G.rng, 38, 58);  e->vx = 1.4f;  e->vz = -0.3f;  e->strength = 1.0f; }
    else                         { lon = rnd_range(&G.rng, -30, 10);  lat = rnd_range(&G.rng, -45, -20); e->vx = 1.6f;  e->vz = 0.1f;   e->strength = 1.1f; }
    e->x = lon2x(lon); e->z = lat2z(lat); e->radius = rnd_range(&G.rng, 10, 18);
    snprintf(e->text, sizeof e->text, carib ? "A hurricane is forming in the Caribbean" : "A great storm is brewing at sea");
    log_event_at(EV_STORM, -1, 0, e->x, e->z, "%s", e->text);
}

// ---------------------------------------------------------------------------
// diplomacy

void set_war(int a, int b, int war, const char *reason) {
    if (a == b || a == NATION_PIRATES || b == NATION_PIRATES || a == NATION_NONE || b == NATION_NONE) return;
    if (G.nations[a].relation[b] == war) return;
    G.nations[a].relation[b] = G.nations[b].relation[a] = war;
    G.nations[a].war_timer[b] = G.nations[b].war_timer[a] = 0;
    G.nations[a].captures_this_war[b] = G.nations[b].captures_this_war[a] = 0;
    if (war) {
        log_event(EV_WAR, -1, 1, "WAR: %s declares war on %s%s%s", nations_info[a].name, nations_info[b].name, reason ? " - " : "", reason ? reason : "");
        // fleets caught in now-hostile harbours are expelled
        for (int i = 0; i < G.nfleets; ++i) { fleet_t *f = &G.fleets[i]; if (!f->alive || f->at_port < 0) continue;
            int pn = G.ports[f->at_port].nation;
            if ((f->nation == a && pn == b) || (f->nation == b && pn == a)) { log_event_at(EV_WAR, f->nation, 0, f->x, f->z, "%s is expelled from %s", f->name, G.ports[f->at_port].name); fleet_expel(f); } }
    } else log_event(EV_PEACE, -1, 1, "PEACE: %s and %s sign a treaty", nations_info[a].name, nations_info[b].name);
}

int ai_accepts_peace(int ai, int with) {
    return G.nations[ai].war_timer[with] > 120 || G.nations[ai].treasury < 30000 || G.nations[with].captures_this_war[ai] >= 2;
}

typedef struct hist_war_t { int year, month, a, b, war; const char *reason; } hist_war_t;
static const hist_war_t history[] = {
    { 1621, 4, NATION_SPAIN, NATION_NETHERLANDS, 1, "the Twelve Years' Truce expires" },
    { 1624, 1, NATION_PORTUGAL, NATION_NETHERLANDS, 1, "the WIC attacks Brazil" },
    { 1625, 9, NATION_SPAIN, NATION_ENGLAND, 1, "the Cadiz expedition" },
    { 1627, 6, NATION_FRANCE, NATION_ENGLAND, 1, "the Huguenot question" },
    { 1629, 4, NATION_FRANCE, NATION_ENGLAND, 0, 0 },
    { 1630, 11, NATION_SPAIN, NATION_ENGLAND, 0, 0 },
    { 1635, 5, NATION_FRANCE, NATION_SPAIN, 1, "Richelieu enters the Thirty Years' War" },
    { 1645, 6, NATION_OTTOMAN, NATION_VENICE, 1, "the siege of Candia" },
    { 1648, 1, NATION_SPAIN, NATION_NETHERLANDS, 0, 0 },
    { 1652, 7, NATION_ENGLAND, NATION_NETHERLANDS, 1, "the Navigation Acts" },
    { 1654, 4, NATION_ENGLAND, NATION_NETHERLANDS, 0, 0 },
    { 1655, 1, NATION_ENGLAND, NATION_SPAIN, 1, "the Western Design" },
    { 1659, 11, NATION_FRANCE, NATION_SPAIN, 0, 0 },
    { 1660, 9, NATION_ENGLAND, NATION_SPAIN, 0, 0 },
    { 1661, 8, NATION_PORTUGAL, NATION_NETHERLANDS, 0, 0 },
    { 1665, 3, NATION_ENGLAND, NATION_NETHERLANDS, 1, "the second Anglo-Dutch war" },
    { 1667, 7, NATION_ENGLAND, NATION_NETHERLANDS, 0, 0 },
    { 1669, 9, NATION_OTTOMAN, NATION_VENICE, 0, 0 },
    { 1672, 4, NATION_FRANCE, NATION_NETHERLANDS, 1, "the Rampjaar" },
    { 1672, 4, NATION_ENGLAND, NATION_NETHERLANDS, 1, "the third Anglo-Dutch war" },
    { 1674, 2, NATION_ENGLAND, NATION_NETHERLANDS, 0, 0 },
    { 1678, 8, NATION_FRANCE, NATION_NETHERLANDS, 0, 0 },
};
#define HISTORY_N ((int)(sizeof(history)/sizeof(history[0])))

static void scripted_history(void) {
    int y, m, d; day2date(G.day, &y, &m, &d);
    while (G.history_cursor < HISTORY_N) {
        const hist_war_t *h = &history[G.history_cursor];
        if (y > h->year || (y == h->year && m >= h->month)) { set_war(h->a, h->b, h->war, h->reason); G.history_cursor++; } else break;
    }
}

// ---------------------------------------------------------------------------
// random events

static void apply_good_event(port_t *pt, int g, float mul_prod, float mul_cons, float days) {
    if (mul_prod > 0) pt->prod_mul[g] = mul_prod;
    if (mul_cons > 0) pt->cons_mul[g] = mul_cons;
    pt->event_timer[g] = days;
}

static void random_events(float dt) {
    unsigned *r = &G.rng;
    int month = day2month(G.day);
    if (rnd_chance(r, P_STORM * (month >= 7 && month <= 10 ? 1.6f : month <= 2 || month == 12 ? 1.3f : 1.f), dt)) event_spawn_storm(-1);

    if (rnd_chance(r, P_PLAGUE, dt)) { int p = port_random(2, -1); if (p >= 0) { port_t *pt = &G.ports[p];
        event_t *e = event_add(EV_PLAGUE, 60); e->port = p; pt->pop *= 0.82f; pt->prosperity *= 0.8f;
        for (int g = 0; g < MAX_GOODS; ++g) apply_good_event(pt, g, 0, 0.5f, 60);
        snprintf(e->text, sizeof e->text, "Plague in %s", pt->name);
        log_event_at(EV_PLAGUE, pt->nation, 0, pt->x, pt->z, "Plague strikes %s: thousands perish, markets close", pt->name); } }

    if (rnd_chance(r, P_HARVEST, dt)) { int p = port_random(1, -1); if (p >= 0) { port_t *pt = &G.ports[p];
        int gs[MAX_GOODS], n = 0; for (int g = 0; g < MAX_GOODS; ++g) if (pt->produce[g] > 0) gs[n++] = g;
        if (n) { int g = gs[rnd_int(r, n)], good = rnd_f(r) < 0.5f;
            apply_good_event(pt, g, good ? 1.9f : 0.35f, 0, 90);
            event_t *e = event_add(good ? EV_HARVEST : EV_FAMINE, 90); e->port = p; e->good = g;
            snprintf(e->text, sizeof e->text, "%s %s in %s", good ? "Bumper" : "Failed", goods[g].name, pt->name);
            log_event_at(good ? EV_HARVEST : EV_FAMINE, pt->nation, 0, pt->x, pt->z, good ? "Bumper %s production in %s floods the market" : "%s production collapses in %s - prices soar", goods[g].name, pt->name); } } }

    if (rnd_chance(r, P_STRIKE, dt)) { int cand[MAX_PORTS], n = 0; for (int i = 0; i < G.nports; ++i) if (G.ports[i].produce[G_BULLION] > 0 && G.ports[i].size > 0) cand[n++] = i;
        if (n) { port_t *pt = &G.ports[cand[rnd_int(r, n)]]; apply_good_event(pt, G_BULLION, 2.6f, 0, 180);
            event_t *e = event_add(EV_STRIKE, 180); e->port = (int)(pt - G.ports); snprintf(e->text, sizeof e->text, "Silver strike near %s", pt->name);
            log_event_at(EV_STRIKE, pt->nation, 0, pt->x, pt->z, "A rich silver vein is struck in the hills behind %s", pt->name); } }

    if (rnd_chance(r, P_FASHION, dt)) { int nat = 1 + rnd_int(r, NATION_PIRATES-1), g = G_WINE + rnd_int(r, MAX_GOODS - G_WINE);
        for (int i = 0; i < G.nports; ++i) if (G.ports[i].nation == nat && !port_colonial(&G.ports[i])) apply_good_event(&G.ports[i], g, 0, 2.2f, 120);
        event_t *e = event_add(EV_FASHION, 120); e->nation = nat; e->good = g; snprintf(e->text, sizeof e->text, "%s craze for %s", nations_info[nat].adj, goods[g].name);
        log_event(EV_FASHION, nat, 0, "%s society is seized by a craze for %s", nations_info[nat].adj, goods[g].name); }

    if (rnd_chance(r, P_COLONISTS, dt)) { int p = port_random(1, -1); if (p >= 0 && port_colonial(&G.ports[p])) { port_t *pt = &G.ports[p]; pt->pop *= 1.3f;
        log_event_at(EV_COLONISTS, pt->nation, 0, pt->x, pt->z, "A shipload of settlers arrives at %s", pt->name); } }

    if (rnd_chance(r, P_FIRE, dt)) { int p = port_random(1, -1); if (p >= 0) { port_t *pt = &G.ports[p];
        for (int g = 0; g < MAX_GOODS; ++g) pt->stock[g] *= 0.5f;
        int b = rnd_int(r, MAX_BUILDINGS); if (pt->building[b] > 0 && rnd_f(r) < 0.5f) pt->building[b]--;
        log_event_at(EV_FIRE, pt->nation, 0, pt->x, pt->z, "Fire ravages the warehouses of %s", pt->name); } }

    if (rnd_chance(r, P_REBELLION, dt)) { int p = port_random(1, -1); if (p >= 0) { port_t *pt = &G.ports[p];
        if ((pt->unrest > 0.45f || pt->prosperity < 0.5f) && pt->nation != NATION_NONE && pt->nation != NATION_PIRATES) {
            log_event_at(EV_REBELLION, pt->nation, 0, pt->x, pt->z, "%s rises in revolt and throws off %s rule", pt->name, nations_info[pt->nation].adj);
            port_set_owner(pt, NATION_NONE); pt->unrest = 0; } } }

    if (rnd_chance(r, P_RANDOM_WAR, dt)) { int a = 1 + rnd_int(r, NATION_PIRATES-1), b = 1 + rnd_int(r, NATION_PIRATES-1);
        if (a != b && !G.nations[a].relation[b]) set_war(a, b, 1, "a dispute over trading rights"); }
    for (int a = 1; a < NATION_PIRATES; ++a) for (int b = a+1; b < NATION_PIRATES; ++b) if (G.nations[a].relation[b]) {
        G.nations[a].war_timer[b] += dt; G.nations[b].war_timer[a] += dt;
        if (G.nations[a].war_timer[b] > PEACE_MIN_DAYS && !is_player(a) && !is_player(b) && rnd_chance(r, P_PEACE, dt)) set_war(a, b, 0, 0);
        // AI at war with the player offers peace when it is exhausted
        if ((is_player(a) || is_player(b)) && G.nations[a].war_timer[b] > PEACE_MIN_DAYS * 2 && rnd_chance(r, P_PEACE, dt)) set_war(a, b, 0, 0);
    }
}

void events_tick(float dt) {
    scripted_history();
    for (int i = 0; i < G.nevents; ) {
        event_t *e = &G.events[i];
        if (e->type == EV_STORM) { e->x += e->vx * dt; e->z += e->vz * dt; }
        if (G.day >= e->day_end) event_remove(i); else ++i;
    }
    random_events(dt);
}
