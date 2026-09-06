// Settlements (see settlement.h). Sections: site potential from climate/terrain · founding
// (site_check / site_create / settlement_found) · production from potential and labour ·
// cultures (normalisation, immigration, assimilation, unrest, secession) · merging (survivor =
// larger population, owner = dominant culture, full reference remap) · daily tick.
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "settlement.h"
#include "economy.h"
#include "fleet.h"
#include "events.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../core/diag.h"
#include "../data/tables.h"
#include "../world/world.h"
#include "../world/nav.h"
#include "../world/regions.h"
#include "region.h"

// ---------------------------------------------------------------------------
// site potential from climate & terrain

static float land_fraction(float lon, float lat, float radius_deg) {   // how much land around (0..1)
    int n = 0, land = 0;
    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) { n++; land += world_is_land(lon2x(lon + dx*radius_deg/2), lat2z(lat + dy*radius_deg/2)); }
    return land / (float)n;
}
static float max_height_near(float lon, float lat, float radius_deg) {
    float h = 0;
    for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) h = maxf(h, world_height(lon2x(lon + dx*radius_deg/3), lat2z(lat + dy*radius_deg/3)));
    return h;
}

void site_potential(float lon, float lat, float pot[MAX_GOODS]) {
    for (int g = 0; g < MAX_GOODS; ++g) pot[g] = 0;
    float a = fabsf(lat);
    float forest = world_fbm(lon*0.15f + 5, lat*0.15f + 9, 3, 3001);     // 0..1
    float dry    = world_fbm(lon*0.11f + 77, lat*0.11f + 3, 3, 3002);
    float land   = land_fraction(lon, lat, 1.0f);
    float mount  = clampf((max_height_near(lon, lat, 1.5f) - 4.f) / 5.f, 0, 1);
    pot[G_FISH]   = 0.6f + 0.6f * (1 - land);                                  // open coasts fish more
    pot[G_TIMBER] = clampf(forest * 1.6f, 0, 1.5f) * (a < 62 ? 1.f : 0.2f) * (a > 8 ? 1.f : 0.6f);
    pot[G_GRAIN]  = a >= 25 && a <= 55 ? 1.0f + 0.5f*(1-dry) : a < 25 ? 0.5f : a < 62 ? 0.4f : 0;
    pot[G_SALT]   = a >= 12 && a <= 38 && dry > 0.45f ? 0.6f + (dry - 0.45f) * 2 : 0;
    pot[G_WINE]   = a >= 30 && a <= 46 ? 0.8f + 0.6f*dry : 0;
    pot[G_SUGAR]  = a < 25 ? (a >= 8 ? 1.2f : 0.8f) * (0.7f + 0.6f*(1-dry)) : 0;
    pot[G_TOBACCO]= a >= 10 && a <= 40 ? 0.7f + 0.5f*forest : 0;
    pot[G_SPICES] = a < 12 ? 0.8f + 0.6f*forest : 0;
    pot[G_FURS]   = a > 47 ? clampf((a - 47) / 12.f, 0.3f, 1.f) * (0.5f + forest) : 0;
    pot[G_BULLION]= mount > 0.2f ? mount * 1.3f : 0;
    // manufacturing needs a city: established later by size (see settlement_update_production)
    pot[G_CLOTH] = 0; pot[G_ARMS] = 0;
    for (int g = 0; g < MAX_GOODS; ++g) if (pot[g] < 0.3f) pot[g] = 0;   // drop marginal resources: keeps the menu short
}

// ---------------------------------------------------------------------------
// founding

int site_check(float wx, float wz, int nation, char *reason, int n) {
    float hx = wx, hz = wz;
    if (!world_is_land(wx, wz)) { snprintf(reason, n, "Colonies are founded on land"); return 0; }
    if (!nav_snap(&hx, &hz, SITE_WATER_REACH) || dist2d(hx, hz, wx, wz) > 3.5f) { snprintf(reason, n, "No harbour: too far from navigable water"); return 0; }
    float lat = z2lat(wz); if (fabsf(lat) > 66) { snprintf(reason, n, "Too far north: the sea is ice"); return 0; }
    for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i]; if (port_is_gone(p)) continue;
        float d = dist2d(p->x, p->z, hx, hz);
        if (d < MIN_SITE_DIST) { snprintf(reason, n, "Too close to %s", p->name); return 0; } }
    if (G.nports >= MAX_PORTS) { snprintf(reason, n, "The world is full of settlements"); return 0; }
    int r = region_at(wx, wz);
    if (r >= 0 && G.regions[r].nhub >= MAX_REGION_PORTS) { int war = 0; for (int k = 0; k < G.regions[r].nhub; ++k) war |= at_war(nation, G.ports[G.regions[r].hub[k]].nation);
        if (!war) { snprintf(reason, n, "The %s already trades through %d ports", G.regions[r].name, MAX_REGION_PORTS); return 0; } }
    (void)nation; reason[0] = 0; return 1;
}

const char *settlement_generate_name(int nation, float lon, float lat) {
    static const char *suffix[] = { "Haven", "Esperanza", "Bay", "Point", "Landing", "Cruz", "Rock", "Sound", "Vista", "Cape", "Hope", "Concord", "Refuge", "Gate", "Marina", "Reach" };
    static char buf[NAME_LEN];
    unsigned h = (unsigned)(lon * 131.f) * 2654435761u ^ (unsigned)(lat * 97.f) * 40503u;
    snprintf(buf, sizeof buf, "%s %s", nations_info[nation].colony_prefix, suffix[(h >> 7) % (sizeof suffix / sizeof suffix[0])]);
    return buf;
}

int site_create(float wx, float wz, int nation) {
    char reason[96]; if (!site_check(wx, wz, nation, reason, sizeof reason)) return -1;
    port_t *p = &G.ports[G.nports];
    memset(p, 0, sizeof *p);
    p->lon = x2lon(wx); p->lat = z2lat(wz); p->lx = wx; p->lz = wz;
    p->x = wx; p->z = wz; nav_snap(&p->x, &p->z, SITE_WATER_REACH);
    p->merged_into = -1; p->founded = 1; p->founded_day = G.day; p->size = 0; p->hinterland = region_at(wx, wz);
    p->nation = NATION_NONE; p->preferred_nation = nation; p->settle_year = day2year(G.day);
    // region from geography
    float lon = p->lon, lat = p->lat;
    p->region = lat > 60 ? REG_ARCTIC : lon < -30 && lat > 10 && lat < 33 && lon > -100 ? REG_CARIB : lon < -30 && lat >= 33 ? REG_NAMER : lon < -30 && lat <= 10 ? REG_SAMER
              : lon < -100 ? REG_PACIFIC : lat < 36 && lon > -20 && lon < 35 && lat > -40 ? REG_AFRICA : lon > 30 ? REG_INDIAN : lon > -6 && lat < 46 && lat > 30 ? REG_MED : lat > 50 ? REG_NORTH : REG_ATLANTIC;
    if (lon > -30 && lat < -34) p->region = REG_AFRICA;
    site_potential(lon, lat, p->potential);
    int k = 0; for (int g = 0; g < MAX_GOODS; ++g) k += p->potential[g] > 0;
    for (int g = 0; g < MAX_GOODS; ++g) { p->labor[g] = p->potential[g] > 0 ? 1.f / k : 0; p->prod_mul[g] = 1; p->cons_mul[g] = 1; }
    snprintf(p->name, sizeof p->name, "%s", settlement_generate_name(nation, lon, lat));
    for (int i = 0; i < G.nports; ++i) if (!strcmp(G.ports[i].name, p->name)) { char base[NAME_LEN]; snprintf(base, sizeof base, "%.16s", settlement_generate_name(nation, lon, lat)); base[16] = 0; snprintf(p->name, sizeof p->name, "%.16s %d", base, G.nports % 1000); break; }   // disambiguate duplicates
    G.nports++;
    nav_ports_added(G.nports - 1);   // one dijkstra row instead of invalidating all cached distances
    LOG_INFO("settle", "site %s created at %.2f,%.2f (%d resources) for nation %d", p->name, lon, lat, k, nation);
    return G.nports - 1;
}

void settlement_init_from_tags(port_t *p) {
    int k = 0;
    for (int g = 0; g < MAX_GOODS; ++g) { p->potential[g] = p->produce[g] > 0 ? p->produce[g] / (TAG_PRODUCTION * maxi(p->size, 1)) : 0; k += p->potential[g] > 0; }
    // give old-world towns a little of what their climate offers so labour can be shifted
    if (k == 0) { site_potential(p->lon, p->lat, p->potential); for (int g = 0; g < MAX_GOODS; ++g) k += p->potential[g] > 0; }
    for (int g = 0; g < MAX_GOODS; ++g) p->labor[g] = p->potential[g] > 0 ? 1.f / maxi(k, 1) : 0;
    for (int n = 0; n < MAX_NATIONS; ++n) p->culture[n] = 0;
    p->culture[p->nation] = p->pop;
    p->merged_into = -1;
    p->pop_cap = port_colonial(p) ? POPCAP_BASE + POPCAP_POTENTIAL * 4 + 30 * p->size : POPCAP_EUROPE;
    settlement_update_production(p);
}

void settlement_found(port_t *p, int nation, float settlers) {
    p->size = 1; p->pop = settlers; p->prosperity = 0.9f;
    if (p->hinterland >= 0) { region_t *R = &G.regions[p->hinterland]; int have = 0; for (int k = 0; k < R->nhub; ++k) have |= R->hub[k] == (int)(p - G.ports);
        if (!have && R->nhub < MAX_REGION_PORTS) { R->hub[R->nhub++] = (int)(p - G.ports); for (int k = 0; k < R->nhub; ++k) G.ports[R->hub[k]].hub_share = 1.f / R->nhub; } }
    for (int n = 0; n < MAX_NATIONS; ++n) p->culture[n] = 0;
    p->culture[nation] = settlers;
    p->fort_max = FORT_BASE_HP + FORT_SIZE_HP; p->fort_hp = p->fort_max * 0.5f;
    float pot = 0; for (int g = 0; g < MAX_GOODS; ++g) pot += p->potential[g];
    p->pop_cap = POPCAP_BASE + POPCAP_POTENTIAL * pot;
    port_set_owner(p, nation);
    settlement_update_production(p);
}

// ---------------------------------------------------------------------------
// production

void settlement_update_production(port_t *p) {
    int k = 0; for (int g = 0; g < MAX_GOODS; ++g) k += p->potential[g] > 0;
    // cities manufacture: cloth & arms potential grows with size (old-world towns had it from tags already)
    float base = TAG_PRODUCTION * maxi(p->size, 1);
    for (int g = 0; g < MAX_GOODS; ++g) {
        float pot = p->potential[g];
        if ((g == G_CLOTH || g == G_ARMS) && pot == 0 && p->size >= 3) pot = 0.5f;
        float colonial_mul = port_colonial(p) && (g == G_SUGAR || g == G_TOBACCO || g == G_BULLION || g == G_FURS) ? COLONIAL_TAG_MUL : 1.f;
        // equal shares over k goods reproduce the legacy output; shifting labour trades one good for another
        p->produce[g] = pot > 0 ? base * pot * colonial_mul * p->labor[g] * maxi(k, 1) : 0;
    }
}

void settlement_set_labor(port_t *p, int good, float share) {
    if (p->potential[good] <= 0) return;
    share = clampf(share, 0, 1);
    float others = 0; for (int g = 0; g < MAX_GOODS; ++g) if (g != good && p->potential[g] > 0) others += p->labor[g];
    int nothers = 0; for (int g = 0; g < MAX_GOODS; ++g) nothers += g != good && p->potential[g] > 0;
    for (int g = 0; g < MAX_GOODS; ++g) if (g != good && p->potential[g] > 0) p->labor[g] = others > 1e-4f ? p->labor[g] / others * (1 - share) : (1 - share) / maxi(nothers, 1);
    p->labor[good] = share;
    settlement_update_production(p);
}

void settlement_auto_labor(port_t *p) {   // drift labour toward goods that are dear here and cheap to make
    float w[MAX_GOODS], sum = 0;
    for (int g = 0; g < MAX_GOODS; ++g) { w[g] = p->potential[g] > 0 ? powf(p->price[g] / goods[g].base_price, 0.5f) * p->potential[g] : 0; sum += w[g]; }
    if (sum <= 0) return;
    for (int g = 0; g < MAX_GOODS; ++g) if (p->potential[g] > 0) p->labor[g] = lerpf(p->labor[g], w[g] / sum, 0.1f);
    settlement_update_production(p);
}

// ---------------------------------------------------------------------------
// cultures

int settlement_dominant_culture(const port_t *p) { int best = p->nation; float b = -1; for (int n = 0; n < MAX_NATIONS; ++n) if (p->culture[n] > b) { b = p->culture[n]; best = n; } return best; }
float settlement_foreign_share(const port_t *p) { float tot = 0; for (int n = 0; n < MAX_NATIONS; ++n) tot += p->culture[n]; return tot > 0 ? 1 - p->culture[p->nation] / tot : 0; }

static void culture_normalize(port_t *p) {   // keep the culture split consistent with the population figure
    float tot = 0; for (int n = 0; n < MAX_NATIONS; ++n) tot += p->culture[n];
    if (tot <= 1e-4f) { p->culture[p->nation] = p->pop; return; }
    float k = p->pop / tot; for (int n = 0; n < MAX_NATIONS; ++n) p->culture[n] *= k;
}

static void settlement_daily(port_t *p, float dt) {
    if (p->size <= 0 || port_is_gone(p)) return;
    if (p->pop_cap <= 0) p->pop_cap = port_colonial(p) ? POPCAP_BASE + 40 : POPCAP_EUROPE;
    culture_normalize(p);
    // immigration: prosperous places with room attract settlers; colonies more than the old world
    float room = maxf(0, p->pop_cap * (1 + 0.3f*p->building[B_MARKET] + 0.2f*p->building[B_WAREHOUSE]) - p->pop);
    float attract = clampf(p->prosperity - 0.7f, 0, 1.5f) * (port_colonial(p) ? 1.f : 0.25f) * (p->blockaded_by ? 0 : 1);
    float arrivals = room * IMMIGRATION_RATE * attract * dt;
    p->immigration = dt > 0 ? arrivals / dt : 0;
    if (arrivals > 0) {
        p->pop += arrivals;
        // most come from the owner's homeland, the rest from the nations that trade here
        float foreign = arrivals * IMMIGRATION_FOREIGN * clampf(p->traffic / 8.f, 0, 1);
        p->culture[p->nation] += arrivals - foreign;
        if (foreign > 0) {   // foreign settlers come from the nations whose ships actually call here (fleets docked now + lane partners)
            float w[MAX_NATIONS] = {0}, tot = 0; int pi = (int)(p - G.ports);
            for (int i = 0; i < G.nfleets; ++i) { const fleet_t *o = &G.fleets[i]; if (o->alive && o->at_port == pi && o->nation != p->nation && o->nation != NATION_PIRATES) w[o->nation] += 1; }
            for (int i = 0; i < G.nlanes; ++i) { const lane_t *l = &G.lanes[i]; if (l->traffic < 0.5f || (l->a != pi && l->b != pi)) continue;
                int other = l->a == pi ? l->b : l->a; int n = G.ports[other].nation; if (n != p->nation && n != NATION_PIRATES) w[n] += l->traffic * 0.25f; }
            for (int n = 0; n < MAX_NATIONS; ++n) tot += w[n];
            if (tot > 0) { float r = rnd_f(&G.rng) * tot; for (int n = 0; n < MAX_NATIONS; ++n) { r -= w[n]; if (r <= 0) { p->culture[n] += foreign; foreign = 0; break; } } }
            p->culture[p->nation] += foreign;   // no partners: they were countrymen after all
        }
    }
    // assimilation and unrest
    float tol = nations_info[p->nation].tolerance, unrest_d = 0;
    for (int n = 0; n < MAX_NATIONS; ++n) {
        if (n == p->nation || p->culture[n] <= 0) continue;
        float conv = p->culture[n] * ASSIMILATION_RATE * tol * dt; p->culture[n] -= conv; p->culture[p->nation] += conv;
        float share = p->culture[n] / maxf(p->pop, 0.1f);
        unrest_d += share * UNREST_CULTURE * (1 - tol * 0.7f) * (n != NATION_NONE && at_war(n, p->nation) ? UNREST_WAR_MUL : 1.f) * dt / (1 + FORT_GARRISON_CALM * p->building[B_FORT]);   // a garrison keeps order
    }
    p->unrest = clampf(p->unrest + unrest_d - 0.001f * dt, 0, 1);
    // secession: a foreign majority in an unhappy settlement declares for its own nation
    int dom = settlement_dominant_culture(p);
    if (dom != p->nation && settlement_foreign_share(p) > REBELLION_FOREIGN && p->unrest > 0.6f && rnd_chance(&G.rng, 0.01f, dt)) {
        int old = p->nation, to = dom == NATION_PIRATES ? NATION_NONE : dom;
        log_event_at(EV_REBELLION, old, log_player_involved(old, to), p->x, p->z, "Civil rebellion in %s: the %s settlers overthrow %s rule%s%s", p->name, nations_info[dom].adj, nations_info[old].adj,
                     to == NATION_NONE ? " and declare independence" : " and raise the flag of ", to == NATION_NONE ? "" : nations_info[to].name);
        port_set_owner(p, to); p->unrest = 0.2f; p->capture_cooldown = CAPTURE_COOLDOWN;
        for (int i = 0; i < G.nfleets; ++i) { fleet_t *f = &G.fleets[i]; if (f->alive && f->at_port == (int)(p - G.ports) && at_war(f->nation, to)) fleet_expel(f); }
    }
    if (!is_player(p->nation) && rnd_chance(&G.rng, 0.05f, dt)) settlement_auto_labor(p);
    settlement_update_production(p);
}

// ---------------------------------------------------------------------------
// merging

static void remap_port_refs(int from, int to) {
    for (int i = 0; i < G.nfleets; ++i) { fleet_t *f = &G.fleets[i]; if (!f->alive) continue;
        if (f->at_port == from) { f->at_port = to; f->x = G.ports[to].x; f->z = G.ports[to].z; }
        if (f->target_port == from) f->target_port = to;
        if (f->home_port == from) f->home_port = to;
        for (int r = 0; r < f->nroute; ++r) if (f->route[r] == from) f->route[r] = to; }
    for (int i = 0; i < G.nlanes; ++i) { if (G.lanes[i].a == from) G.lanes[i].a = to; if (G.lanes[i].b == from) G.lanes[i].b = to; if (G.lanes[i].a == G.lanes[i].b) G.lanes[i].traffic = 0; }
    for (int n = 0; n < MAX_NATIONS; ++n) if (G.nations[n].capital == from) G.nations[n].capital = to;
    for (int i = 0; i < G.nevents; ++i) if (G.events[i].port == from) G.events[i].port = to;
}

static void merge_settlements(int ai, int bi) {
    port_t *a = &G.ports[ai], *b = &G.ports[bi];
    if (b->pop > a->pop) { port_t *t = a; a = b; b = t; int ti = ai; ai = bi; bi = ti; }   // a survives
    int old_owner = a->nation;
    for (int n = 0; n < MAX_NATIONS; ++n) a->culture[n] += b->culture[n];
    a->pop += b->pop; a->pop_cap += b->pop_cap * 0.6f;
    for (int g = 0; g < MAX_GOODS; ++g) { a->stock[g] += b->stock[g]; a->potential[g] = maxf(a->potential[g], b->potential[g]); }
    for (int k = 0; k < MAX_BUILDINGS; ++k) a->building[k] = maxi(a->building[k], b->building[k]);
    a->traffic += b->traffic; a->tariffs_collected += b->tariffs_collected; a->fort_max += b->fort_max * 0.3f; a->fort_hp += b->fort_hp * 0.3f;
    int kk = 0; for (int g = 0; g < MAX_GOODS; ++g) kk += a->potential[g] > 0;
    for (int g = 0; g < MAX_GOODS; ++g) a->labor[g] = a->potential[g] > 0 ? 1.f / maxi(kk, 1) : 0;
    // ownership follows the dominant culture of the combined population
    int dom = settlement_dominant_culture(a); if (dom == NATION_PIRATES) dom = NATION_NONE;
    b->size = 0; b->merged_into = ai; b->pop = 0; b->nation = NATION_NONE;
    if (b->region >= 0) { region_t *R = &G.regions[b->region]; for (int k = 0; k < R->nhub; ++k) if (R->hub[k] == bi) { R->hub[k] = R->hub[--R->nhub]; break; } }
    remap_port_refs(bi, ai);
    nations_recount_ports();
    if (dom != old_owner) { port_set_owner(a, dom); a->unrest = clampf(a->unrest + MERGE_UNREST_SHOCK, 0, 1); }
    else a->unrest = clampf(a->unrest + MERGE_UNREST_SHOCK * settlement_foreign_share(a), 0, 1);
    a->size = a->pop > SIZE_CITY_POP ? 3 : a->pop > SIZE_TOWN_POP ? 2 : 1;
    settlement_update_production(a); port_update_prices(a); territory_dirty = 1;   // distances of the survivor are unchanged
    int involved = log_player_involved(old_owner, b->preferred_nation) || is_player(dom);
    log_event_at(EV_COLONY, dom, involved, a->x, a->z, "%s absorbs the neighbouring settlement of %s; the %s majority %s", a->name, b->name, nations_info[dom].adj,
                 dom != old_owner ? "takes control of the merged town" : "keeps it under the same flag");
    LOG_INFO("settle", "merged %s into %s: owner %d -> %d, pop %.1fk", b->name, a->name, old_owner, dom);
}

static void merge_check(void) {
    for (int i = 0; i < G.nports; ++i) { port_t *a = &G.ports[i]; if (a->size <= 0 || port_is_gone(a)) continue;
        for (int j = i + 1; j < G.nports; ++j) { port_t *b = &G.ports[j]; if (b->size <= 0 || port_is_gone(b)) continue;
            if (!a->founded && !b->founded) continue;                                         // historical cities never merge
            if ((a->founded && G.day - a->founded_day < MERGE_MIN_AGE) || (b->founded && G.day - b->founded_day < MERGE_MIN_AGE)) continue;
            if (dist2d(a->x, a->z, b->x, b->z) > MERGE_DIST) continue;
            if (at_war(a->nation, b->nation) || a->blockaded_by || b->blockaded_by) continue;     // rivals at war stay apart
            if (a->nation == NATION_PIRATES || b->nation == NATION_PIRATES) continue;
            merge_settlements(i, j); return;                                                  // one per day keeps indices simple
        } }
}

void settlements_tick(float dt) {
    static int p_s = -1; if (p_s < 0) p_s = prof_id("sim.settlements");
    prof_begin(p_s);
    for (int i = 0; i < G.nports; ++i) settlement_daily(&G.ports[i], dt);
    if ((int)G.day != (int)(G.day - dt)) merge_check();   // once per simulated day (state-derived: deterministic across save/load)
    prof_end(p_s);
}
