// Headless simulation test: builds the simulation without the v3 framework or any GL,
// runs several years for several nations and checks invariants.
//
//   cc game/tests/sim_test.c -o sim_test -lm -O2 && ./sim_test [years] [seed]

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   // must precede the first system header (clock_gettime, isatty)
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../src/core/jobs.h"      // linked from src/core/jobs.cpp: nav's precompute runs on the pool
#include "../src/core/diag.c"
#include "../src/core/log.c"
#include "../src/world/world.c"
#include "../src/world/nav.c"
#include "../src/world/wind.c"
#include "../src/world/regions.c"
#include "../src/sim/economy.c"
#include "../src/sim/events.c"
#include "../src/sim/fleet.c"
#include "../src/sim/ai.c"
#include "../src/sim/combat.c"
#include "../src/sim/settlement.c"
#include "../src/sim/region.c"
#include "../src/sim/game.c"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int toasts;
static void listener(const logline_t *l) { if (l->important) toasts++; }

static void check_invariants(const char *when) {
    for (int i = 0; i < G.nports; ++i) { const port_t *p = &G.ports[i];
        for (int g = 0; g < MAX_GOODS; ++g) { CHECK(isfinite(p->stock[g]) && p->stock[g] >= 0, "%s: %s stock[%d]=%f", when, p->name, g, p->stock[g]);
            CHECK(isfinite(p->price[g]) && p->price[g] > 0, "%s: %s price[%d]=%f", when, p->name, g, p->price[g]); }
        CHECK(isfinite(p->pop) && p->pop >= 0, "%s: %s pop=%f", when, p->name, p->pop);
        CHECK(p->nation >= 0 && p->nation < MAX_NATIONS, "%s: %s nation=%d", when, p->name, p->nation);
        if (p->size > 0) CHECK(p->pop > 0.4f, "%s: %s settled but no population", when, p->name); }
    int fleets = 0, on_land = 0;
    for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i]; if (!f->alive) continue; fleets++;
        CHECK(f->nships > 0 && f->nships <= MAX_SHIPS, "%s: fleet %s has %d ships", when, f->name, f->nships);
        CHECK(isfinite(f->x) && isfinite(f->z), "%s: fleet %s position nan", when, f->name);
        if (f->at_port < 0 && f->order != ORDER_IDLE && world_is_land(f->x, f->z)) { on_land++; if (getenv("VERBOSE")) printf("  on land: %s order %d at %.2f,%.2f pathi %d/%d\n", f->name, f->order, x2lon(f->x), z2lat(f->z), f->pathi, f->npath); }
        CHECK(fleet_cargo(f) <= fleet_capacity(f) + 1, "%s: fleet %s overloaded %.0f/%.0f", when, f->name, fleet_cargo(f), fleet_capacity(f));
        if (f->target_fleet_id) CHECK(fleet_by_id(f->target_fleet_id) || f->order != ORDER_ATTACK, "%s: %s attacks a dead fleet", when, f->name); }
    CHECK(on_land * 50 <= fleets, "%s: %d of %d fleets are sailing over land", when, on_land, fleets);
    int counted = 0; for (int n = 0; n < MAX_NATIONS; ++n) { counted += G.nations[n].fleets; CHECK(isfinite(G.nations[n].treasury), "%s: nation %d treasury nan", when, n); }
    CHECK(counted == fleets, "%s: nation fleet counters %d != alive fleets %d", when, counted, fleets);
    for (int a = 0; a < MAX_NATIONS; ++a) for (int b = 0; b < MAX_NATIONS; ++b) CHECK(G.nations[a].relation[b] == G.nations[b].relation[a], "%s: asymmetric relation %d/%d", when, a, b);
}

int main(int argc, char **argv) {
    int years = argc > 1 ? atoi(argv[1]) : 3; unsigned seed = argc > 2 ? (unsigned)atoi(argv[2]) : 7;
    diag_config_t dc = { "test_logs", LOG_ERROR, LOG_DEBUG, LOG_TRACE, 0 }; diag_init(&dc);
    // The real pool, not synchronous mode: the port-distance precompute runs in parallel here,
    // so the save/load determinism check below is also the determinism check for that work.
    jobs_init(-1); atexit(jobs_shutdown);
    world_init(seed); nav_init();
    log_set_listener(listener);

    // connectivity of key routes
    struct { const char *a, *b; int reachable; } routes[] = { {"Amsterdam","Havana",1}, {"Amsterdam","Stockholm",1}, {"Venice","Constantinople",1}, {"Lisbon","Callao",1}, {"Lisbon","Mombasa",1}, {"Quebec","London",1}, {"Archangel","London",1}, {"Alexandria","Jeddah",1} };   // the Red Sea reaches the Indian Ocean around the Horn (map-edge channel)
    game_new(NATION_NETHERLANDS, seed);
    for (unsigned i = 0; i < sizeof routes / sizeof routes[0]; ++i) {
        int a = port_by_name(routes[i].a), b = port_by_name(routes[i].b);
        CHECK(a >= 0 && b >= 0, "unknown port %s/%s", routes[i].a, routes[i].b);
        float len = nav_path(G.ports[a].x, G.ports[a].z, G.ports[b].x, G.ports[b].z, 0, 0, 0, 0);
        CHECK((len > 0) == routes[i].reachable, "route %s -> %s reachable=%d (len %.0f)", routes[i].a, routes[i].b, len > 0, len);
    }
    for (int i = 0; i < G.nports; ++i) { int nx, ny; CHECK(nav_nearest(G.ports[i].x, G.ports[i].z, &nx, &ny, 2), "port %s has no water nearby", G.ports[i].name); }
    { int hub = port_by_name("Amsterdam");   // every port must be reachable from the main ocean (an isolated harbour traps fleets forever)
      for (int i = 0; i < G.nports; ++i) if (i != hub) CHECK(nav_port_dist(hub, i) > 0, "port %s is not reachable from %s", G.ports[i].name, G.ports[hub].name); }

    // run a few nations
    int nations[] = { NATION_NETHERLANDS, NATION_SPAIN, NATION_ENGLAND };
    for (int k = 0; k < 3; ++k) {
        game_new(nations[k], seed + k);
        check_invariants("start");
        double t0 = G.nations[nations[k]].treasury;
        for (int y = 0; y < years; ++y) { game_advance(365); char buf[32]; snprintf(buf, sizeof buf, "year %d", 1620 + y); check_invariants(buf); }
        int settled = 0; for (int i = 0; i < G.nports; ++i) settled += G.ports[i].size > 0;
        printf("%-18s %d years: treasury %.0f -> %.0f | fleets %d trades %d battles %d captures %d lanes %d settled %d toasts %d\n",
               nations_info[nations[k]].name, years, t0, G.nations[nations[k]].treasury, game_count_fleets(), G.trades, G.battles, G.captures, G.nlanes, settled, toasts);
        CHECK(G.trades > 1000 * years, "too little trade: %d", G.trades);
        CHECK(G.nlanes > 20, "too few lanes: %d", G.nlanes);
        CHECK(game_count_fleets() > 40, "fleet count collapsed: %d", game_count_fleets());
    }

    // hinterlands: coverage, hubs, deposits, inland settlements, development dynamics
    game_new(NATION_ENGLAND, 7);
    { int land = 0, covered = 0; for (int i = 0; i < TERR_W*TERR_H; ++i) { int l = landmask[((i/TERR_W)*TERR_DIV+TERR_DIV/2)*LM_W + (i%TERR_W)*TERR_DIV+TERR_DIV/2]; land += l; covered += l && regionmap[i]; }
      CHECK(covered == land, "every land cell belongs to a region (%d of %d)", covered, land);
      int with = 0, settled = 0, rare = 0; for (int i = 0; i < G.nports; ++i) if (G.ports[i].size > 0) { settled++; with += G.ports[i].hinterland >= 0; }
      CHECK(with * 10 >= settled * 9, "most settled ports need a hinterland (%d of %d)", with, settled);
      for (int r = 0; r < G.nregions; ++r) { const region_t *R = &G.regions[r]; for (int g = G_IRON; g <= G_GOLD; ++g) rare += R->potential[g] > 0;
          if (R->nhub) CHECK(R->pop > 0 && R->development > 0, "region %s with hubs must be populated", R->name);
          for (int k = 0; k < R->nhub; ++k) CHECK(G.ports[R->hub[k]].hinterland == r, "hub %s points to another region", G.ports[R->hub[k]].name); }
      CHECK(rare >= 15, "too few rare deposits: %d", rare);
      CHECK(G.ninland >= 10, "too few initial inland settlements: %d", G.ninland);
      int potosi = region_at(lon2x(-65.7f), lat2z(-19.6f)); CHECK(potosi >= 0 && G.regions[potosi].potential[G_SILVER] >= 1.5f, "Potosi silver pin missing");
      int tidewater = G.ports[port_by_name("Jamestown")].hinterland; float dev0 = G.regions[tidewater].development;
      game_advance(365);
      CHECK(G.regions[tidewater].development != dev0, "region development must evolve (Tidewater %.2f -> %.2f)", dev0, G.regions[tidewater].development);
      int moved = 0; for (int i = 0; i < G.nports; ++i) for (int g = 0; g < MAX_GOODS; ++g) moved += G.ports[i].region_in[g] > 0.01f;
      CHECK(moved > 50, "hub links should deliver goods into port markets (%d port/good flows)", moved);
      check_invariants("regions"); }

    // colonisation: free placement, production, merging by dominant culture
    game_new(NATION_NETHERLANDS, 5);
    { char reason[96];
      float wx = lon2x(-88.0f), wz = lat2z(30.4f);                       // Mobile Bay, gulf coast: free
      CHECK(site_check(wx, wz, NATION_NETHERLANDS, reason, sizeof reason), "gulf coast should be settleable: %s", reason);
      CHECK(!site_check(lon2x(-82.4f), lat2z(23.1f), NATION_NETHERLANDS, reason, sizeof reason), "next to Havana must be refused");
      CHECK(!site_check(lon2x(-60.f), lat2z(30.f), NATION_NETHERLANDS, reason, sizeof reason), "open sea must be refused");
      int a = site_create(wx, wz, NATION_NETHERLANDS); CHECK(a >= 0, "site_create failed");
      port_t *pa = &G.ports[a]; int npot = 0; for (int g = 0; g < MAX_GOODS; ++g) npot += pa->potential[g] > 0;
      CHECK(npot >= 2 && pa->potential[G_TOBACCO] > 0, "gulf coast site should offer tobacco among %d resources", npot);
      CHECK(pa->region == REG_CARIB || pa->region == REG_NAMER, "region guess %d", pa->region);
      CHECK(pa->hinterland == region_at(pa->lx, pa->lz), "new site must preserve its generated hinterland independently of its geographic trade region");
      settlement_found(pa, NATION_NETHERLANDS, 2.0f);
      CHECK(pa->size == 1 && pa->nation == NATION_NETHERLANDS && pa->culture[NATION_NETHERLANDS] == 2.0f, "founding");
      float before = port_production(pa, G_TOBACCO); settlement_set_labor(pa, G_TOBACCO, 1.0f);
      CHECK(port_production(pa, G_TOBACCO) > before * 1.5f, "all labour on tobacco must raise output (%.2f -> %.2f)", before, port_production(pa, G_TOBACCO));
      float sum = 0; for (int g = 0; g < MAX_GOODS; ++g) sum += pa->labor[g]; CHECK(fabsf(sum - 1) < 1e-3f, "labour shares must sum to 1 (%.3f)", sum);
      // a bigger spanish settlement next door -> merge, spanish majority takes control
      int b = site_create(lon2x(-87.0f), lat2z(30.4f), NATION_SPAIN); CHECK(b >= 0, "second site");
      settlement_found(&G.ports[b], NATION_SPAIN, 6.0f);
      G.ports[a].founded_day = G.ports[b].founded_day = -1000;           // old enough to merge
      int before_ports = 0; for (int i = 0; i < G.nports; ++i) before_ports += G.ports[i].size > 0;
      game_advance(3);
      int after_ports = 0; for (int i = 0; i < G.nports; ++i) after_ports += G.ports[i].size > 0;
      CHECK(after_ports == before_ports - 1, "neighbouring colonies should have merged (%d -> %d)", before_ports, after_ports);
      port_t *s = port_is_gone(&G.ports[a]) ? &G.ports[b] : &G.ports[a];
      CHECK(port_is_gone(&G.ports[a]) != port_is_gone(&G.ports[b]), "exactly one settlement survives");
      CHECK(s->nation == NATION_SPAIN, "the spanish majority should own the merged town (owner %d)", s->nation);
      CHECK(s->culture[NATION_NETHERLANDS] > 1.5f && s->pop > 7.5f, "cultures and population must be summed (%.1f dutch, pop %.1f)", s->culture[NATION_NETHERLANDS], s->pop);
      CHECK(s->unrest > 0.05f, "a merge that changes owner must cause unrest");
      // immigration into a prosperous colony
      s->prosperity = 1.6f; float pop0 = s->pop; game_advance(30);
      CHECK(s->pop > pop0, "prosperous colony should attract immigrants (%.2f -> %.2f)", pop0, s->pop);
      check_invariants("colonies"); }

    // save / load round trip must be exact and deterministic
    game_new(NATION_FRANCE, 99); game_advance(100);
    CHECK(game_save("test_save.bin"), "save failed");
    static game_t snapshot, after_a, before;   // ~1 MB each: never on the stack (Windows default stack is 1 MB)
    snapshot = G;
    game_advance(50); after_a = G;
    CHECK(game_load("test_save.bin"), "load failed");
    CHECK(memcmp(&G, &snapshot, sizeof G) == 0, "loaded state differs from saved state");
    game_advance(50);
    CHECK(G.day == after_a.day && G.trades == after_a.trades && G.battles == after_a.battles && G.nlog == after_a.nlog, "simulation after load is not deterministic (%d vs %d trades)", G.trades, after_a.trades);
    remove("test_save.bin");

    // asynchronous save / load (game.h): the file written on the IO lane is byte-identical to
    // the blocking save's, a load is refused while a save is in flight, a save requested during
    // a save is queued and also written, no temp file is left behind, and a state applied by
    // game_io_poll continues exactly as the blocking path does.
    { game_io_result_t r; int done = 0;
      CHECK(game_save("test_sync.bin"), "sync save failed");
      CHECK(game_save_async("test_async.bin"), "async save refused");
      CHECK(game_io_state() == GAME_IO_SAVE, "a save should be in flight until polled");
      CHECK(!game_load_async("test_async.bin"), "a load must be refused while a save is in flight");
      CHECK(game_save_async("test_async2.bin"), "a second save should be queued, not refused");
      while (game_io_state() != GAME_IO_IDLE) { if (game_io_poll(&r)) { done++; CHECK(r.kind == GAME_IO_SAVE && r.ok, "async save %d failed (%s)", done, r.path); CHECK(r.snapshot_ms < 50, "snapshot took %.1f ms", r.snapshot_ms); } else jobs_help(); }
      CHECK(done == 2, "expected two completed saves, got %d", done);
      { static game_t a, b; const char *names[2] = { "test_async.bin", "test_async2.bin" };
        FILE *fa = fopen("test_sync.bin", "rb"); CHECK(fa && fread(&a, sizeof a, 1, fa) == 1, "cannot read back the sync save"); if (fa) fclose(fa);
        for (int k = 0; k < 2; ++k) { FILE *fb = fopen(names[k], "rb"); CHECK(fb && fread(&b, sizeof b, 1, fb) == 1, "cannot read back %s", names[k]); if (fb) fclose(fb);
          CHECK(memcmp(&a, &b, sizeof a) == 0, "%s differs from the blocking save", names[k]);
          char tmp[64]; snprintf(tmp, sizeof tmp, "%s.tmp", names[k]); FILE *ft = fopen(tmp, "rb"); CHECK(!ft, "%s was left behind", tmp); if (ft) fclose(ft); } }
      snapshot = G; game_advance(50); after_a = G;
      CHECK(game_load_async("test_async.bin"), "async load refused");
      CHECK(!game_save_async("test_never.bin"), "a save must be refused while a load is in flight");
      while (!game_io_poll(&r)) jobs_help();
      CHECK(r.kind == GAME_IO_LOAD && r.ok, "async load failed");
      CHECK(memcmp(&G, &snapshot, sizeof G) == 0, "state applied by the async load differs from what was saved");
      game_advance(50);
      CHECK(G.day == after_a.day && G.trades == after_a.trades && G.battles == after_a.battles && G.nlog == after_a.nlog, "simulation after an async load is not deterministic (%d vs %d trades)", G.trades, after_a.trades);
      before = G;
      CHECK(game_load_async("test_missing.bin"), "a load of a missing file should start and then fail");
      while (!game_io_poll(&r)) jobs_help();
      CHECK(r.kind == GAME_IO_LOAD && !r.ok, "a missing file must be reported as a failed load");
      CHECK(memcmp(&G, &before, sizeof G) == 0, "a failed async load modified the state");
      remove("test_sync.bin"); remove("test_async.bin"); remove("test_async2.bin"); }
    int errors_before = diag_level_count(LOG_ERROR);
    FILE *bad = fopen("test_bad.bin", "wb"); fputs("garbage", bad); fclose(bad);
    before = G; CHECK(!game_load("test_bad.bin"), "garbage save was accepted"); CHECK(memcmp(&G, &before, sizeof G) == 0, "failed load modified the state"); remove("test_bad.bin");
    CHECK(diag_level_count(LOG_ERROR) == errors_before + 1, "rejecting a corrupt save must log exactly one error");

    CHECK(metric_value(metric_id("nav.astar_unreachable")) * 20 < metric_value(metric_id("nav.astar_calls")), "too many unreachable path requests: %.0f of %.0f", metric_value(metric_id("nav.astar_unreachable")), metric_value(metric_id("nav.astar_calls")));
    CHECK(diag_level_count(LOG_ERROR) == errors_before + 1, "%d unexpected errors were logged during the simulation", diag_level_count(LOG_ERROR) - errors_before - 1);
    printf("nav: %.0f A* calls, %.0f unreachable, %.0f nodes, %.0f dijkstra rows | log: %llu records, %d warnings\n", metric_value(metric_id("nav.astar_calls")), metric_value(metric_id("nav.astar_unreachable")), metric_value(metric_id("nav.astar_nodes")), metric_value(metric_id("nav.dijkstra_rows")), diag_log_total(), diag_level_count(LOG_WARN));
    diag_shutdown();
    printf("%s (%d failures) [%d regions]\n", failures ? "FAILED" : "OK", failures, (int)(sizeof(region_names)/sizeof(region_names[0])));
    return failures ? 1 : 0;
}
