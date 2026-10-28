// Headless probe (DESIGN_VISUAL.md step 1): how often does terrain hide a town from the RTS
// camera? For every living port as the look-at target, cameras at three distances / pitches
// and eight yaws; count the other towns in front of the camera that the terrain-ray test
// (gfx/occlusion.c) hides, and 32x32-unit land boxes hidden too. Result on seed 1: 1 town of
// 37,311 views, 0 of 53,686 boxes - the relief tops out at 7 units and the camera never pitches
// below 12 degrees. That is why the shipped occlusion culling is the GPU query against the
// depth buffer (gfx/occquery.c) and the terrain ray is only kept for towns and ships.
//
// Build like sim_test (needs jobs.cpp linked; from the repository root):
//   c++ -std=c++17 -c game/src/core/jobs.cpp -o jobs.o && cc tools/occ_probe.c jobs.o -o occ_probe -lm -lpthread -lstdc++
//   cl /std:c11 tools\occ_probe.c jobs.obj  (after cl /c /std:c++17 game\src\core\jobs.cpp)
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../game/src/core/jobs.h"
#include "../game/src/core/diag.c"
#include "../game/src/core/log.c"
#include "../game/src/world/world.c"
#include "../game/src/world/nav.c"
#include "../game/src/world/wind.c"
#include "../game/src/world/regions.c"
#include "../game/src/sim/economy.c"
#include "../game/src/sim/events.c"
#include "../game/src/sim/fleet.c"
#include "../game/src/sim/ai.c"
#include "../game/src/sim/combat.c"
#include "../game/src/sim/settlement.c"
#include "../game/src/sim/region.c"
#include "../game/src/sim/game.c"
#include "../game/src/gfx/occlusion.c"

// the land-box test the probe measured (0 hits): all four top corners and the centre hidden,
// samples inside the box itself ignored
static int occ_box_hidden(occ_height_fn h, float x0, float z0, float x1, float z1, float top) {
    const float box[4] = { x0, z0, x1, z1 };
    return occ_ray_hidden(h, (x0 + x1) * 0.5f, top, (z0 + z1) * 0.5f, 0, box) && occ_ray_hidden(h, x0, top, z0, 0, box)
        && occ_ray_hidden(h, x1, top, z0, 0, box) && occ_ray_hidden(h, x0, top, z1, 0, box) && occ_ray_hidden(h, x1, top, z1, 0, box);
}

int main(void) {
    diag_config_t dc = { "probe_logs", LOG_ERROR, LOG_ERROR, LOG_ERROR, 0 }; diag_init(&dc);
    jobs_init(0);
    world_init(1); nav_init(); game_new(4, 1);
    float hmax = 0; for (int i = 0; i < LM_W*LM_H; ++i) if (heightmap[i] > hmax) hmax = heightmap[i];
    printf("heightmap max %.2f, %d ports\n", hmax, G.nports);
    const float dists[3] = { 14, 40, 120 }; const float pitches[3] = { 12, 22, 35 };
    long total_tests = 0, total_hidden = 0, total_chunk_tests = 0, total_chunk_hidden = 0;
    struct { int port, d, p, yaw, hidden, inview, chunks, chunktests; } best[8]; memset(best, 0, sizeof best);
    for (int pi = 0; pi < G.nports; ++pi) { const port_t *P = &G.ports[pi]; if (port_is_gone(P) || P->size < 1) continue;
      for (int di = 0; di < 3; ++di) for (int yi = 0; yi < 8; ++yi) {
        float dist = dists[di], pitch = pitches[di] * 0.0174533f, yaw = yi * 45.f * 0.0174533f;
        float gx = sinf(yaw), gz = -cosf(yaw), cp = cosf(pitch), sp = sinf(pitch);
        float eye[3] = { P->x - gx*dist*cp, dist*sp, P->z - gz*dist*cp };
        float fx = gx, fz = gz;   // look direction in xz
        occ_frame_begin(eye, hmax);
        int inview = 0, hidden = 0, ch = 0, cht = 0;
        for (int j = 0; j < G.nports; ++j) { const port_t *Q = &G.ports[j]; if (j == pi || port_is_gone(Q) || Q->size < 1) continue;
            float dx = Q->x - eye[0], dz = Q->z - eye[2], d = sqrtf(dx*dx + dz*dz); if (d > dist * 2.5f + 20.f) continue;
            if ((dx*fx + dz*fz) / d < 0.7f) continue;   // ~45 degree half-angle
            inview++;
            if (occ_sphere_hidden(world_height, Q->x, Q->z, 3.5f, world_height(Q->x, Q->z) + 6.f)) hidden++; }
        // terrain chunk boxes within range in front
        float range = dist * 2.5f + 20.f;
        for (float cz = floorf((eye[2] - range) / 32.f) * 32.f; cz < eye[2] + range; cz += 32.f) for (float cx = floorf((eye[0] - range) / 32.f) * 32.f; cx < eye[0] + range; cx += 32.f) {
            float mx = cx + 16, mz = cz + 16, dx = mx - eye[0], dz = mz - eye[2], d = sqrtf(dx*dx + dz*dz); if (d < 1 || d > range || (dx*fx + dz*fz) / d < 0.6f) continue;
            float top = -1e9f, mn = 1e9f; for (int yy = 0; yy <= 8; ++yy) for (int xx = 0; xx <= 8; ++xx) { float h = world_height(cx + xx*4.f, cz + yy*4.f); if (h > top) top = h; if (h < mn) mn = h; }
            if (top < 0.f) continue;   // water only
            cht++; if (occ_box_hidden(world_height, cx, cz, cx + 32, cz + 32, top)) ch++; }
        total_tests += inview; total_hidden += hidden; total_chunk_tests += cht; total_chunk_hidden += ch;
        int score = hidden * 10 + ch;
        for (int b = 0; b < 8; ++b) if (score > best[b].hidden * 10 + best[b].chunks) { for (int k = 7; k > b; --k) best[k] = best[k-1]; best[b].port = pi; best[b].d = di; best[b].p = di; best[b].yaw = yi * 45; best[b].hidden = hidden; best[b].inview = inview; best[b].chunks = ch; best[b].chunktests = cht; break; }
      } }
    printf("towns in view %ld, hidden %ld (%.1f%%) | land chunks in view %ld, hidden %ld (%.1f%%) | %d heightmap reads\n", total_tests, total_hidden, 100.0 * total_hidden / (total_tests ? total_tests : 1), total_chunk_tests, total_chunk_hidden, 100.0 * total_chunk_hidden / (total_chunk_tests ? total_chunk_tests : 1), occ_samples);
    for (int b = 0; b < 8; ++b) { const port_t *P = &G.ports[best[b].port];
        printf("  %-16s lon %.1f lat %.1f dist %.0f pitch %.0f yaw %d: towns hidden %d of %d, chunks hidden %d of %d  -> MARE_CAM=%.2f,%.2f,%.0f,%d\n", P->name, x2lon(P->x), z2lat(P->z), dists[best[b].d], pitches[best[b].p], best[b].yaw, best[b].hidden, best[b].inview, best[b].chunks, best[b].chunktests, x2lon(P->x), z2lat(P->z), dists[best[b].d], best[b].yaw); }
    return 0;
}
