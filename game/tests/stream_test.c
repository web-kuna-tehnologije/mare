// Headless test of world streaming (world/stream.h): grid shape, wanted set, loading through
// the real job pool, delta determinism, the disk cache round-trip, the coast fade that keeps
// the simulation's ground fixed, the memory budget and the LRU unloading policy.
//
//   cc game/tests/stream_test.c + core/jobs.cpp (see game/build.sh "test")

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "../src/core/jobs.h"      // linked from src/core/worker_pool.cc
#include "../src/core/diag.h"      // linked from src/core/diag.cc
#include "../src/core/log.c"
#include "../src/world/world.c"
#include "../src/world/stream.c"

game_t G;   // world.c references G for territory; the simulation itself is not linked here

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define TILE_DIR "test_tiles"

// run updates and the pool until nothing is loading (bounded; the pool must drain long before)
static void settle(float cx, float cz, float dist) {
    for (int it = 0; it < 20000; ++it) {
        stream_update(cx, cz, dist);
        stream_stats_t s; stream_stats(&s);
        if (s.loading == 0) return;
        jobs_help();
    }
    CHECK(0, "streaming did not settle (tiles stuck loading)");
}

static void clean_tiles(void) {
    char path[128];
    for (int l = 0; l < STREAM_LEVELS; ++l)
        for (int ty = 0; ty < 40; ++ty) for (int tx = 0; tx < 40; ++tx) {
            snprintf(path, sizeof path, TILE_DIR "/L%d_%d_%d.mht", l, tx, ty); remove(path);
        }
}

int main(void) {
    diag_config_t dc = { "test_stream_logs", LOG_WARN, LOG_DEBUG, LOG_TRACE, 0 };
    diag_init(&dc);
    jobs_init(-1);
    world_init(7);
    clean_tiles();
    stream_init(7, TILE_DIR);

    // grid shape matches the design table (DESIGN_STREAMING.md section 1)
    CHECK(stream_grid_w(0) == 3 && stream_grid_h(0) == 3, "L0 grid %dx%d (want 3x3)", stream_grid_w(0), stream_grid_h(0));
    CHECK(stream_grid_w(1) == 6 && stream_grid_h(1) == 5, "L1 grid %dx%d (want 6x5)", stream_grid_w(1), stream_grid_h(1));
    CHECK(stream_grid_w(2) == 11 && stream_grid_h(2) == 9, "L2 grid %dx%d (want 11x9)", stream_grid_w(2), stream_grid_h(2));
    CHECK(stream_grid_w(3) == 22 && stream_grid_h(3) == 17, "L3 grid %dx%d (want 22x17)", stream_grid_w(3), stream_grid_h(3));
    CHECK(stream_tile_count() == 9 + 30 + 99 + 374, "total tiles %d (want 512)", stream_tile_count());
    { stream_stats_t s; stream_stats(&s); CHECK(s.water > 20 && s.water < 400, "water-only tiles %d (mid-ocean must not stream)", s.water); }

    // city view over the Andes: the leaf chunk under the camera must be wanted and become ready
    float ax = lon2x(-65.7f), az = lat2z(-19.6f);   // Potosi: mountainous, far inland
    settle(ax, az, 8.f);
    stream_stats_t s1; stream_stats(&s1);
    CHECK(s1.resident > 0, "nothing became resident");
    CHECK(s1.generated > 0, "first run must generate tiles (empty cache)");
    { float deg = (STREAM_L0_CELLS >> 3) / (float)CELLS_PER_DEG;
      int tx = (int)((x2lon(ax) - LON0) / deg), ty = (int)((LAT1 - z2lat(az)) / deg);
      stream_tile_t *t = stream_tile(3, tx, ty);
      CHECK(t && t->state == TILE_READY, "the tactical chunk under the camera (L3 %d,%d) is not resident", tx, ty); }

    // the delta is alive in the mountains and dead where the simulation builds (coast, water)
    float d_mount = 0; float best = 0;
    for (int k = 0; k < 400; ++k) { float x = ax + (k % 20 - 10) * 0.8f, z = az + (k / 20 - 10) * 0.8f;
        float h = world_height(x, z); float d = stream_delta_at(x, z);
        if (h > 2.f && fabsf(d) > fabsf(best)) best = d;
        if (h < 0.3f) CHECK(d == 0.f, "delta %.3f on low ground (h %.2f): towns would float", d, h);
        if (!world_is_land(x, z)) CHECK(stream_delta_at(x, z) == 0.f, "delta on water"); }
    d_mount = best;
    CHECK(fabsf(d_mount) > 0.01f, "no detail in the mountains (max |delta| %.4f)", fabsf(d_mount));

    // level-clamped lookup (the coarse clipmap ring asks for L2 so its texels never try to
    // carry L3 detail): never finer than asked, and L1 and above carry no delta at all
    { int differ = 0;
      for (int k = 0; k < 400; ++k) { float x = ax + (k % 20 - 10) * 0.8f, z = az + (k / 20 - 10) * 0.8f;
          float all = stream_delta_at(x, z), l2 = stream_delta_at_level(x, z, STREAM_L_COASTAL);
          if (all != l2) differ++;
          CHECK(stream_delta_at_level(x, z, STREAM_L_REGION) == 0.f, "L1 and coarser must carry no delta"); }
      CHECK(differ > 0, "the L2-clamped ring should differ from the finest lookup somewhere"); }

    // chunk reference image (what a map chunk is modelled from, docs/BLENDER_LOD.md section 4)
    { stream_chunk_ref_t cr;
      CHECK(!stream_dump_chunk_reference(3, 9999, 0, 64, "test_chunk.ppm", &cr), "an out-of-range tile must be refused");
      CHECK(stream_dump_chunk_reference(3, 16, 2, 64, "test_chunk.ppm", &cr), "chunk reference failed");
      // degrees are fixed by the tile grid; world units follow WORLD_K, which WORLD_SCALE moves
      CHECK(cr.size_deg == 8.f && cr.size_units == 8.f * WORLD_K,
            "an L3 chunk is 8 degrees / %.0f world units (got %.1f / %.1f)", 8.f * WORLD_K, cr.size_deg, cr.size_units);
      CHECK(cr.lon1 - cr.lon0 == cr.size_deg && cr.lat1 - cr.lat0 == cr.size_deg, "the reference extent must be the tile's");
      CHECK(cr.land_frac > 0.f && cr.land_frac < 1.f, "L3 16,2 straddles a coast (land %.2f)", cr.land_frac);
      CHECK(cr.max_h > cr.min_h, "height range must be real (%.2f..%.2f)", cr.min_h, cr.max_h);
      FILE *f = fopen("test_chunk.ppm", "rb");
      CHECK(f != NULL, "no reference file written");
      if (f) { char hdr[16] = {0}; size_t n = fread(hdr, 1, 15, f); fclose(f);
               CHECK(n > 10 && hdr[0] == 'P' && hdr[1] == '6', "the reference must be a binary PPM"); }
      remove("test_chunk.ppm"); }

    // determinism + disk round-trip: a fresh init with the same seed reloads identical data from disk
    float probe[16]; int np = 0;
    for (int k = 0; k < 16; ++k) probe[np++] = stream_delta_at(ax + (k % 4) * 1.3f, az + (k / 4) * 1.1f);
    stream_shutdown();
    stream_init(7, TILE_DIR);
    settle(ax, az, 8.f);
    stream_stats_t s2; stream_stats(&s2);
    CHECK(s2.disk_hits > 0, "second run must hit the disk cache");
    CHECK(s2.generated == 0, "second run regenerated %d tiles that were cached", s2.generated);
    for (int k = 0; k < np; ++k) { float v = stream_delta_at(ax + (k % 4) * 1.3f, az + (k / 4) * 1.1f);
        CHECK(v == probe[k], "delta not deterministic across restart (probe %d: %.6f vs %.6f)", k, v, probe[k]); }

    // a wrong-seed cache is rejected and regenerated
    stream_shutdown();
    stream_init(8, TILE_DIR);
    settle(ax, az, 8.f);
    { stream_stats_t s; stream_stats(&s); CHECK(s.generated > 0, "a cache written for another seed must be regenerated"); }
    stream_shutdown();
    clean_tiles();

    // budget + unloading policy: a tiny budget forces LRU eviction of tiles the camera left
    stream_init(7, TILE_DIR);
    stream_set_budget((size_t)6 * STREAM_RES * STREAM_RES * sizeof(float));
    settle(ax, az, 8.f);
    float bx = lon2x(-2.f), bz = lat2z(40.f);       // Spain: the other side of the Atlantic
    settle(bx, bz, 8.f);
    for (int k = 0; k < 50; ++k) stream_update(bx, bz, 8.f);   // give the evictor its per-update allowance
    { stream_stats_t s; stream_stats(&s);
      CHECK(s.evicted > 0, "over budget but nothing was evicted");
      // the wanted set itself is never evicted, so that is the floor the policy can reach
      CHECK(s.bytes <= s.budget_bytes + (size_t)s.wanted * STREAM_RES * STREAM_RES * sizeof(float),
            "resident above budget + wanted set (%zu vs %zu + %d tiles)", s.bytes, s.budget_bytes, s.wanted);
      float deg = (STREAM_L0_CELLS >> 3) / (float)CELLS_PER_DEG;
      int tx = (int)((x2lon(bx) - LON0) / deg), ty = (int)((LAT1 - z2lat(bz)) / deg);
      stream_tile_t *t = stream_tile(3, tx, ty);
      CHECK(t && t->state == TILE_READY, "the wanted chunk under the camera must never be evicted"); }

    // a mid-ocean tile stays empty even when wanted
    { float ox = lon2x(-40.f), oz = lat2z(30.f);    // open Atlantic
      settle(ox, oz, 8.f);
      float deg = (STREAM_L0_CELLS >> 3) / (float)CELLS_PER_DEG;
      stream_tile_t *t = stream_tile(3, (int)((x2lon(ox) - LON0) / deg), (int)((LAT1 - z2lat(oz)) / deg));
      if (t && t->water_only) CHECK(t->state == TILE_EMPTY && !t->delta, "a water-only tile was loaded"); }

    stream_shutdown();
    clean_tiles();
    jobs_shutdown();
    diag_shutdown();
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
