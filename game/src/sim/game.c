// Game session (see game.h for the API).
// Sections: world setup (port snapping to harbour + town positions, population/building/stock
// seeding from data tables, nations, starting fleets) · master tick (order matters: economy before
// settlements before fleets; daily stat rollover; territory refresh) · save/load (raw game_t with
// magic/version/size header; caches rebuilt after a load, state untouched on failure).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"
#include "economy.h"
#include "fleet.h"
#include "events.h"
#include "ai.h"
#include "combat.h"
#include "settlement.h"
#include "region.h"
#include "../world/regions.h"
#include "../core/util.h"
#include "../core/balance.h"
#include "../core/log.h"
#include "../core/diag.h"
#include "../data/tables.h"
#include "../data/geo_ports.h"
#include "../world/world.h"
#include "../world/nav.h"

game_t G;

// ---------------------------------------------------------------------------
// world setup

static void port_snap(port_t *p) {
    p->x = lon2x(p->lon); p->z = lat2z(p->lat);
    nav_snap(&p->x, &p->z, 10);
    // town centre: nearest land cell a little inland from the harbour
    int cx = lon2cx(x2lon(p->x)), cy = lat2cy(z2lat(p->z));
    p->lx = p->x; p->lz = p->z;
    for (int r = 1; r <= 12; ++r) for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
        if (abs(dx) != r && abs(dy) != r) continue;
        int x = cx+dx, y = cy+dy; if (x < 1 || y < 1 || x >= LM_W-1 || y >= LM_H-1) continue;
        if (landmask[y*LM_W+x] && coastdist[y*LM_W+x] >= 2) { p->lx = lon2x(cx2lon(x)); p->lz = lat2z(cy2lat(y)); return; }
    }
}

static void setup_ports(void) {
    int n = mini(GEO_NPORTS, MAX_PORTS);
    for (int i = 0; i < n; ++i) {
        const geo_port_t *gp = &geo_ports[i]; port_t *p = &G.ports[i];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", gp->name);
        p->lon = gp->lon; p->lat = gp->lat; p->nation = gp->nation; p->size = gp->size; p->region = gp->region;
        p->preferred_nation = gp->pref_nation; p->settle_year = gp->settle_year; p->haven = gp->haven;
        port_snap(p);
        int col = port_colonial(p);
        p->pop = p->size == 0 ? 0 : p->size == 1 ? (col ? 3 : 9) : p->size == 2 ? (col ? 9 : 35) : (col ? 28 : 120);
        p->pop *= rnd_range(&G.rng, 0.8f, 1.25f);
        p->prosperity = 1;
        for (int g = 0; g < MAX_GOODS; ++g) { p->prod_mul[g] = 1; p->cons_mul[g] = 1; }
        for (const char *t = gp->tags; *t; ++t) { int g = geo_tag_good(*t); if (g < 0) continue;
            int colonial_good = g == G_SUGAR || g == G_TOBACCO || g == G_BULLION || g == G_FURS;
            p->produce[g] += TAG_PRODUCTION * (col && colonial_good ? COLONIAL_TAG_MUL : 1.f) * maxi(p->size, 1); }
        if (p->size >= 2 && !col) p->building[B_DOCKS] = 1;
        if (p->size >= 3) { p->building[B_WAREHOUSE] = 1; p->building[B_MARKET] = 1; }
        if (p->size >= 2 && (!col || gp->nation == NATION_SPAIN || gp->haven)) p->building[B_FORT] = 1;
        p->fort_max = FORT_BASE_HP + FORT_LEVEL_HP * p->building[B_FORT] + FORT_SIZE_HP * p->size; p->fort_hp = p->fort_max;
        settlement_init_from_tags(p);
        if (p->size == 0) { float pot[MAX_GOODS]; site_potential(p->lon, p->lat, pot); for (int k2 = 0; k2 < MAX_GOODS; ++k2) if (p->potential[k2] <= 0 && pot[k2] > 0) p->potential[k2] = pot[k2] * 0.7f; }
        for (int g = 0; g < MAX_GOODS; ++g) p->stock[g] = p->size > 0 ? port_norm(p, g) * rnd_range(&G.rng, 0.7f, 1.3f) : 0;
        port_update_prices(p);
    }
    G.nports = n;
}

static void setup_nations(int player) {
    static const char *capitals[MAX_NATIONS] = { "Hamburg", "Seville", "London", "Le Havre", "Amsterdam", "Lisbon", "Venice", "Constantinople", "Copenhagen", "Tortuga" };
    for (int i = 0; i < MAX_NATIONS; ++i) {
        nation_t *nt = &G.nations[i]; memset(nt, 0, sizeof *nt);
        nt->capital = port_by_name(capitals[i]);
        nt->treasury = 60000 + 30000 * (i == NATION_SPAIN); nt->ai_timer = rnd_range(&G.rng, 1, 10);
    }
    G.nations[player].treasury = 60000;
    log_set_quiet(1);
    set_war(NATION_SPAIN, NATION_OTTOMAN, 1, 0);   // ancient enmity
    G.nations[NATION_SPAIN].war_timer[NATION_OTTOMAN] = G.nations[NATION_OTTOMAN].war_timer[NATION_SPAIN] = 2000;
    log_set_quiet(0);
    nations_recount_ports();
}

static void setup_fleets(int player) {
    for (int nat = NATION_NONE; nat < NATION_PIRATES; ++nat) {
        if (nat == player) continue;
        int nm = nations_info[nat].ai_merchants * 6 / 10, nw = nations_info[nat].ai_warships / 2;
        for (int k = 0; k < nm; ++k) { int p = port_random_of(nat, 1); if (p >= 0) spawn_merchant(nat, p); }
        for (int k = 0; k < nw; ++k) { int p = port_random_of(nat, 2); if (p < 0) p = port_random_of(nat, 1); if (p >= 0) spawn_warfleet(nat, p, 2 + rnd_int(&G.rng, 2)); }
        G.nations[nat].treasury = 60000 + 30000 * (nat == NATION_SPAIN);   // starting fleets are free
    }
    for (int i = 0; i < G.nports; ++i) if (G.ports[i].haven && G.ports[i].size > 0) spawn_pirates(i, 2);
    int cap = G.nations[player].capital; if (cap < 0 || G.ports[cap].nation != player) cap = port_random_of(player, 1);
    if (cap < 0) return;
    port_t *p = &G.ports[cap];
    fleet_t *f = fleet_new(player, ROLE_TRADER, p->x, p->z, "First Fleet");
    if (f) { fleet_add_ship(f, SHIP_FLUYT); fleet_add_ship(f, SHIP_FLUYT); f->at_port = cap; f->home_port = cap; f->order = ORDER_DOCK; f->cargo[G_CLOTH] = 150; f->cargo[G_ARMS] = 40; }
    fleet_t *w = fleet_new(player, ROLE_WARSHIP, p->x, p->z, "Home Squadron");
    if (w) { fleet_add_ship(w, SHIP_FRIGATE); fleet_add_ship(w, SHIP_SLOOP); w->at_port = cap; w->home_port = cap; w->order = ORDER_DOCK; }
}

void game_new(int player, unsigned seed) {
    double t0 = diag_now();
    memset(&G, 0, sizeof G);
    G.magic = SAVE_MAGIC; G.version = SAVE_VERSION; G.size = sizeof G;
    G.player = player; G.seed = seed; G.rng = seed * 2654435761u + 12345u; G.speed = 1;
    G.treasure_timer = 400;   // the first flota sails the first spring
    nav_ports_reset();
    regions_generate(seed, 1);
    setup_ports();
    regions_attach_ports();
    regions_setup();
    setup_nations(player);
    setup_fleets(player);
    world_compute_territory();
    LOG_INFO("game", "new game: player %s seed %u, %d ports, %d fleets (%.1f ms)", nations_info[player].name, seed, G.nports, game_count_fleets(), (diag_now()-t0)*1000);
    log_event(EV_INFO, player, 1, "1 January 1620. You are Admiral of the %s fleet. Build a trading empire.", nations_info[player].adj);
}

// ---------------------------------------------------------------------------
// tick

static int p_eco = -1, p_ev, p_fl, p_cb, p_ai, m_fleets, m_trades, m_battles, m_captures, m_events, m_treasury, m_lanes, m_sunk;
static void game_metrics_init(void) {
    if (p_eco >= 0) return;
    p_eco = prof_id("sim.economy"); p_ev = prof_id("sim.events"); p_fl = prof_id("sim.fleets"); p_cb = prof_id("sim.combat"); p_ai = prof_id("sim.nations_ai");
    m_fleets = metric_id("sim.fleets_alive"); m_trades = metric_id("sim.trades"); m_battles = metric_id("sim.battles"); m_captures = metric_id("sim.captures");
    m_events = metric_id("sim.active_events"); m_treasury = metric_id("sim.player_treasury"); m_lanes = metric_id("sim.lanes"); m_sunk = metric_id("sim.ships_sunk");
}

void game_tick(float dt) {
    game_metrics_init();
    int day_before = (int)G.day;
    G.day += dt;
    diag_set_sim_day(G.day);
    prof_begin(p_eco); economy_tick(dt); prof_end(p_eco);
    settlements_tick(dt);
    regions_tick(dt);
    prof_begin(p_ev);  events_tick(dt);  prof_end(p_ev);
    prof_begin(p_fl);  for (int i = 0; i < G.nfleets; ++i) fleet_tick(&G.fleets[i], dt); prof_end(p_fl);
    prof_begin(p_cb);  combat_tick(dt);  prof_end(p_cb);
    prof_begin(p_ai);  ai_nations_tick(dt); prof_end(p_ai);
    lanes_decay(dt);
    metric_set(m_fleets, game_count_fleets()); metric_set(m_trades, G.trades); metric_set(m_battles, G.battles); metric_set(m_captures, G.captures);
    metric_set(m_events, G.nevents); metric_set(m_treasury, G.nations[G.player].treasury); metric_set(m_lanes, G.nlanes);
    { int sunk = 0; for (int n = 0; n < MAX_NATIONS; ++n) sunk += G.nations[n].ships_sunk; metric_set(m_sunk, sunk); }
    DIAG_CHECK(G.nations[G.player].treasury == G.nations[G.player].treasury, "game", "player treasury is NaN");
    if ((int)G.day != day_before)
        for (int n = 0; n < MAX_NATIONS; ++n) { nation_t *nt = &G.nations[n]; nt->stat_income = nt->income_acc; nt->stat_expense = nt->expense_acc; nt->income_acc = nt->expense_acc = 0; }
    if (territory_dirty == 1) world_compute_territory();
}

void game_advance(float days) {
    int steps = (int)(days / SIM_STEP + 0.5f);
    for (int i = 0; i < steps; ++i) game_tick(SIM_STEP);
}

int game_count_fleets(void) { int k = 0; for (int i = 0; i < G.nfleets; ++i) k += G.fleets[i].alive; return k; }

// ---------------------------------------------------------------------------
// save / load: the whole state struct, guarded by magic, version and size

int game_save(const char *path) {
    FILE *fp = fopen(path, "wb"); if (!fp) { LOG_ERROR("game", "cannot open %s for writing", path); return 0; }
    G.magic = SAVE_MAGIC; G.version = SAVE_VERSION; G.size = sizeof G;
    int ok = fwrite(&G, sizeof G, 1, fp) == 1;
    fclose(fp);
    if (ok) LOG_INFO("game", "saved %s (%u bytes, day %.1f)", path, (unsigned)sizeof G, G.day); else LOG_ERROR("game", "short write saving %s", path);
    return ok;
}

int game_load(const char *path) {
    FILE *fp = fopen(path, "rb"); if (!fp) { LOG_WARN("game", "no save file at %s", path); return 0; }
    game_t *tmp = malloc(sizeof *tmp);
    int ok = fread(tmp, sizeof *tmp, 1, fp) == 1 && tmp->magic == SAVE_MAGIC && tmp->version == SAVE_VERSION && tmp->size == sizeof G;
    fclose(fp);
    if (ok) { G = *tmp; nav_ports_reset(); regions_generate(G.seed, 0); regions_refresh_devmap(); economy_refresh_caches(); world_compute_territory(); LOG_INFO("game", "loaded %s (day %.1f)", path, G.day); }
    else LOG_ERROR("game", "rejected %s: magic %08x version %u size %u (expected %08x %u %u)", path, tmp->magic, tmp->version, tmp->size, SAVE_MAGIC, SAVE_VERSION, (unsigned)sizeof G);
    free(tmp);
    return ok;
}
