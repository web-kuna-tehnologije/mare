// mare::game::sim::Game - the session (see game.h for the C ABI).
// Sections: world setup (port snapping to harbour + town positions, population/building/stock
// seeding from data tables, nations, starting fleets) · master tick (order matters: economy
// before settlements before fleets; daily stat rollover; territory refresh) · save/load (raw
// game_t with magic/version/size header; caches rebuilt after a load, state untouched on
// failure) · the asynchronous save/load the game uses (snapshot on the main thread, file work
// on the IO lane). The POD state stays the C global `G` - it IS the save format.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
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
#include "../core/jobs.h"
}

game_t G;

namespace mare::game::sim {

class Game {
public:
    void new_game(int player, unsigned seed);
    void tick(float dt);
    void advance(float days);
    int  count_fleets();
    int  save(const char *path);
    int  load(const char *path);
    int  save_async(const char *path);
    int  load_async(const char *path);
    game_io_kind_t io_state() { return gio_kind; }
    int  io_poll(game_io_result_t *out);
    void io_flush();

private:
    static void port_snap(port_t *p);
    static void setup_ports();
    static void setup_nations(int player);
    static void setup_fleets(int player);
    void game_metrics_init();
    static int  save_header_ok(const game_t *s);
    static void game_apply_loaded(const game_t *src);
    static int  save_write(const game_t *s, const char *path);
    static int  save_read(game_t *dst, const char *path, int *found);
    static void gio_save_job(void *arg);          // IO-lane callbacks (plain fn signature)
    static void gio_load_job(void *arg);
    int gio_start(game_io_kind_t kind, const char *path, job_fn fn);

    // tick metrics (lazily registered)
    int p_eco = -1, p_ev = 0, p_fl = 0, p_cb = 0, p_ai = 0,
        m_fleets = 0, m_trades = 0, m_battles = 0, m_captures = 0, m_events = 0, m_treasury = 0, m_lanes = 0, m_sunk = 0;
    // asynchronous save / load. The main thread owns gio_kind and the request fields; the IO
    // job owns gio_buf and gio_ok while it runs, and the join counter's completion publishes
    // them back (acquire/release inside the worker pool), so no other synchronisation is needed.
    game_t        *gio_buf = nullptr;              // heap: a game_t never goes on the stack
    game_io_kind_t gio_kind = GAME_IO_IDLE;        // in flight
    job_counter_t  gio_counter = 0;
    char           gio_path[256] = {};
    int            gio_ok = 0;
    double         gio_t0 = 0, gio_snapshot_ms = 0;
    int            gio_pending = 0; char gio_pending_path[256] = {};
    int m_save_ms = -1, m_load_ms = -1, m_snapshot_ms = -1, m_save_count = -1;
};

Game &game() { static Game g; return g; }

// ---------------------------------------------------------------------------
// world setup

void Game::port_snap(port_t *p) {
    p->x = lon2x(p->lon); p->z = lat2z(p->lat);
    nav_snap(&p->x, &p->z, 10);
    // town centre: nearest land cell a little inland from the harbour
    int cx = lon2cx(x2lon(p->x)), cy = lat2cy(z2lat(p->z));
    p->lx = p->x; p->lz = p->z;
    // distances in cells follow the grid: a quarter of a degree inland, searched over 1.5 degrees
    const int inland = CELLS_PER_DEG / 4, reach = (CELLS_PER_DEG * 3) / 2;
    for (int r = 1; r <= reach; ++r) for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
        if (abs(dx) != r && abs(dy) != r) continue;
        int x = cx+dx, y = cy+dy; if (x < 1 || y < 1 || x >= LM_W-1 || y >= LM_H-1) continue;
        if (landmask[y*LM_W+x] && coastdist[y*LM_W+x] >= inland) { p->lx = lon2x(cx2lon(x)); p->lz = lat2z(cy2lat(y)); return; }
    }
}

void Game::setup_ports() {
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

void Game::setup_nations(int player) {
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

void Game::setup_fleets(int player) {
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

void Game::new_game(int player, unsigned seed) {
    double t0 = diag_now();
    memset(&G, 0, sizeof G);
    G.magic = SAVE_MAGIC; G.version = SAVE_VERSION; G.size = sizeof G;
    G.player = player; G.seed = seed; G.rng = seed * 2654435761u + 12345u; G.speed = SIM_SPEED_DEFAULT;
    G.treasure_timer = 400;   // the first flota sails the first spring
    nav_ports_reset();
    regions_generate(seed, 1);
    setup_ports();
    regions_attach_ports();
    regions_setup();
    setup_nations(player);
    setup_fleets(player);
    world_compute_territory();
    // Every port distance now, in parallel, rather than one 15 ms Dijkstra at a time during the
    // first minutes of play. The rows are the same either way, so the simulation is unchanged.
    nav_ports_precompute();
    LOG_INFO("game", "new game: player %s seed %u, %d ports, %d fleets (%.1f ms)", nations_info[player].name, seed, G.nports, count_fleets(), (diag_now()-t0)*1000);
    log_event(EV_INFO, player, 1, "1 January 1620. You are Admiral of the %s fleet. Build a trading empire.", nations_info[player].adj);
}

// ---------------------------------------------------------------------------
// tick

void Game::game_metrics_init() {
    if (p_eco >= 0) return;
    p_eco = prof_id("sim.economy"); p_ev = prof_id("sim.events"); p_fl = prof_id("sim.fleets"); p_cb = prof_id("sim.combat"); p_ai = prof_id("sim.nations_ai");
    m_fleets = metric_id("sim.fleets_alive"); m_trades = metric_id("sim.trades"); m_battles = metric_id("sim.battles"); m_captures = metric_id("sim.captures");
    m_events = metric_id("sim.active_events"); m_treasury = metric_id("sim.player_treasury"); m_lanes = metric_id("sim.lanes"); m_sunk = metric_id("sim.ships_sunk");
}

void Game::tick(float dt) {
    game_metrics_init();
    int day_before = (int)G.day;
    G.day += dt;
    diag_set_sim_day(G.day);
    prof_begin(p_eco); economy_tick(dt); prof_end(p_eco);
    settlements_tick(dt);
    regions_tick(dt);
    prof_begin(p_ev);  events_tick(dt);  prof_end(p_ev);
    prof_begin(p_fl);
    fleets_precompute_speeds();   // wind factors on the pool (DESIGN_STREAMING.md task 5); the tick stays ordered
    for (int i = 0; i < G.nfleets; ++i) fleet_tick(&G.fleets[i], dt);
    prof_end(p_fl);
    prof_begin(p_cb);  combat_tick(dt);  prof_end(p_cb);
    prof_begin(p_ai);  ai_nations_tick(dt); prof_end(p_ai);
    lanes_decay(dt);
    metric_set(m_fleets, count_fleets()); metric_set(m_trades, G.trades); metric_set(m_battles, G.battles); metric_set(m_captures, G.captures);
    metric_set(m_events, G.nevents); metric_set(m_treasury, G.nations[G.player].treasury); metric_set(m_lanes, G.nlanes);
    { int sunk = 0; for (int n = 0; n < MAX_NATIONS; ++n) sunk += G.nations[n].ships_sunk; metric_set(m_sunk, sunk); }
    DIAG_CHECK(G.nations[G.player].treasury == G.nations[G.player].treasury, "game", "player treasury is NaN");
    if ((int)G.day != day_before)
        for (int n = 0; n < MAX_NATIONS; ++n) { nation_t *nt = &G.nations[n]; nt->stat_income = nt->income_acc; nt->stat_expense = nt->expense_acc; nt->income_acc = nt->expense_acc = 0; }
    if (territory_dirty == 1) world_compute_territory();
}

void Game::advance(float days) {
    int steps = (int)(days / SIM_STEP + 0.5f);
    for (int i = 0; i < steps; ++i) tick(SIM_STEP);
}

int Game::count_fleets() { int k = 0; for (int i = 0; i < G.nfleets; ++i) k += G.fleets[i].alive; return k; }

// ---------------------------------------------------------------------------
// save / load: the whole state struct, guarded by magic, version and size

int Game::save_header_ok(const game_t *s) { return s->magic == SAVE_MAGIC && s->version == SAVE_VERSION && s->size == sizeof G; }

// Caches outside G are rebuilt from it (ONBOARDING "State"); the nav rows come back through the
// pool, which is the one measurable cost of a load (~300 ms for 111 ports on 8 chunks).
void Game::game_apply_loaded(const game_t *src) {
    G = *src;
    nav_ports_reset(); nav_ports_precompute(); regions_generate(G.seed, 0); regions_refresh_devmap(); economy_refresh_caches(); world_compute_territory();
}

// Write a complete file beside the target, then replace the target: whatever happens during the
// write, the previous save stays intact. (remove + rename rather than a bare rename, because MSVC's
// rename refuses to overwrite.)
int Game::save_write(const game_t *s, const char *path) {
    char tmp[300]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *fp = fopen(tmp, "wb"); if (!fp) { LOG_ERROR("game", "cannot open %s for writing", tmp); return 0; }
    int ok = fwrite(s, sizeof *s, 1, fp) == 1;
    ok = (fclose(fp) == 0) && ok;
    if (ok) { remove(path); ok = rename(tmp, path) == 0; }
    if (!ok) { LOG_ERROR("game", "writing %s failed", path); remove(tmp); }
    return ok;
}

int Game::save_read(game_t *dst, const char *path, int *found) {
    FILE *fp = fopen(path, "rb"); *found = fp != NULL;
    if (!fp) { LOG_WARN("game", "no save file at %s", path); return 0; }
    int ok = fread(dst, sizeof *dst, 1, fp) == 1 && save_header_ok(dst);
    fclose(fp);
    if (!ok) LOG_ERROR("game", "rejected %s: magic %08x version %u size %u (expected %08x %u %u)", path, dst->magic, dst->version, dst->size, SAVE_MAGIC, SAVE_VERSION, (unsigned)sizeof G);
    return ok;
}

int Game::save(const char *path) {
    G.magic = SAVE_MAGIC; G.version = SAVE_VERSION; G.size = sizeof G;
    int ok = save_write(&G, path);
    if (ok) LOG_INFO("game", "saved %s (%u bytes, day %.1f)", path, (unsigned)sizeof G, G.day);
    return ok;
}

int Game::load(const char *path) {
    game_t *tmp = (game_t *)malloc(sizeof *tmp); int found;
    int ok = save_read(tmp, path, &found);
    if (ok) { game_apply_loaded(tmp); LOG_INFO("game", "loaded %s (day %.1f)", path, G.day); }
    free(tmp);
    return ok;
}

// ---------------------------------------------------------------------------
// asynchronous save / load (game.h): state lives on the class, above.

void Game::gio_save_job(void *arg) { (void)arg; Game &g = game(); g.gio_ok = save_write(g.gio_buf, g.gio_path); }
void Game::gio_load_job(void *arg) { (void)arg; int found; Game &g = game(); g.gio_ok = save_read(g.gio_buf, g.gio_path, &found); }

int Game::gio_start(game_io_kind_t kind, const char *path, job_fn fn) {
    if (!gio_buf) gio_buf = (game_t *)malloc(sizeof *gio_buf);
    snprintf(gio_path, sizeof gio_path, "%s", path);
    gio_kind = kind; gio_ok = 0; gio_t0 = diag_now(); gio_snapshot_ms = 0;
    if (kind == GAME_IO_SAVE) {
        G.magic = SAVE_MAGIC; G.version = SAVE_VERSION; G.size = sizeof G;
        *gio_buf = G;                              // the only work the frame pays for
        gio_snapshot_ms = (diag_now() - gio_t0) * 1000;
    }
    gio_counter = jobs_counter();
    if (gio_counter) jobs_submit(JOB_LANE_IO, fn, NULL, gio_counter);
    else fn(NULL);                                 // counter slab exhausted (never in practice): do it here, blocking, rather than lose track of the buffer
    return 1;
}

int Game::save_async(const char *path) {
    if (!path || !*path) return 0;
    if (gio_kind == GAME_IO_LOAD) { LOG_WARN("game", "save of %s refused: a load is in flight", path); return 0; }
    if (gio_kind == GAME_IO_SAVE) { gio_pending = 1; snprintf(gio_pending_path, sizeof gio_pending_path, "%s", path); return 1; }
    return gio_start(GAME_IO_SAVE, path, gio_save_job);
}

int Game::load_async(const char *path) {
    if (!path || !*path) return 0;
    if (gio_kind != GAME_IO_IDLE) { LOG_WARN("game", "load of %s refused: %s in flight", path, gio_kind == GAME_IO_SAVE ? "a save is" : "a load is already"); return 0; }
    gio_pending = 0;
    return gio_start(GAME_IO_LOAD, path, gio_load_job);
}

int Game::io_poll(game_io_result_t *out) {
    if (gio_kind == GAME_IO_IDLE) return 0;
    if (gio_counter && !jobs_done(gio_counter)) return 0;
    if (gio_counter) jobs_release(gio_counter);
    gio_counter = 0;
    game_io_result_t r; memset(&r, 0, sizeof r);
    r.kind = gio_kind; r.ok = gio_ok; snprintf(r.path, sizeof r.path, "%s", gio_path);
    if (m_save_ms < 0) { m_save_ms = metric_id("io.save_ms"); m_load_ms = metric_id("io.load_ms"); m_snapshot_ms = metric_id("save.snapshot_ms"); m_save_count = metric_id("save.count"); }
    if (r.kind == GAME_IO_LOAD) {
        if (r.ok) { double t = diag_now(); game_apply_loaded(gio_buf); r.snapshot_ms = (diag_now() - t) * 1000; }
        r.ms = (diag_now() - gio_t0) * 1000;
        if (r.ok) LOG_INFO("game", "loaded %s (day %.1f) in %.1f ms: read on the IO lane, state applied and caches rebuilt in %.1f ms", r.path, G.day, r.ms, r.snapshot_ms);
        metric_set(m_load_ms, r.ms);
    } else {
        r.ms = (diag_now() - gio_t0) * 1000; r.snapshot_ms = gio_snapshot_ms;
        if (r.ok) LOG_INFO("game", "saved %s (%u bytes, day %.1f): snapshot %.2f ms on the main thread, written on the IO lane after %.1f ms", r.path, (unsigned)sizeof G, gio_buf->day, r.snapshot_ms, r.ms);
        metric_set(m_save_ms, r.ms); metric_set(m_snapshot_ms, r.snapshot_ms); if (r.ok) metric_add(m_save_count, 1);
    }
    gio_kind = GAME_IO_IDLE;
    if (gio_pending) { gio_pending = 0; save_async(gio_pending_path); }
    if (out) *out = r;
    return 1;
}

void Game::io_flush() {
    while (gio_kind != GAME_IO_IDLE) if (!io_poll(NULL)) jobs_help();   // jobs_help sleeps briefly when the IO threads hold the work
}

} // namespace mare::game::sim

// C ABI (game.h) for the unported callers
using mare::game::sim::game;
extern "C" {
void game_new(int player, unsigned seed) { game().new_game(player, seed); }
void game_tick(float dt) { game().tick(dt); }
void game_advance(float days) { game().advance(days); }
int  game_count_fleets(void) { return game().count_fleets(); }
int  game_save(const char *path) { return game().save(path); }
int  game_load(const char *path) { return game().load(path); }
int  game_save_async(const char *path) { return game().save_async(path); }
int  game_load_async(const char *path) { return game().load_async(path); }
game_io_kind_t game_io_state(void) { return game().io_state(); }
int  game_io_poll(game_io_result_t *out) { return game().io_poll(out); }
void game_io_flush(void) { game().io_flush(); }
}
