// Streaming view (game/docs/DESIGN_STREAMING.md tasks 3 and 7): feeds the world streamer with
// the camera, composes the finest resident tiles into one R32F "detail window" the terrain
// shader adds on top of the base heightmap, marks tiles that have authored meshes, draws the
// tile grid as a debug overlay (the list of grids still to model, in place, in the world), and
// publishes the streaming metrics.
//
// The window is composed with jobs_parallel_for on the WORKER lane from the main thread, so it
// never races a tile eviction (evictions only happen inside stream_update, also on the main
// thread); at 512x512 it costs well under a millisecond across the pool and is only rebuilt
// when the residency changes or the camera leaves the inner half of the window.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "camera.h"
#include "scene.h"
#include "lod.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../world/world.h"
#include "../world/stream.h"
#include "../assets/asset.h"

#define SV_RES     512
#define SV_EXTENT  80.f              // world units the detail window covers
#define SV_MIN_RECOMPOSE_S 0.20     // residency bursts (a tile a frame) fold into one rebuild

int stream_show_tiles;               // dashboard toggle: draw the tile grid overlay

static rhi_texture_t sv_tex;
static float *sv_buf;
static int    sv_inited, sv_on;
static float  sv_x0, sv_z0;          // window origin (world); extent is SV_EXTENT
static unsigned sv_seen_residency, sv_seen_assets;
static unsigned sv_world_seed = 0xFFFFFFFFu;
static double sv_last_compose = -1e9;
static double sv_window_ms;
static int    sv_tiles_unmodeled;    // wanted land leaves without an authored mesh (dashboard)

// one row band of the window; reads tile payloads which only ever appear (never vanish) while
// the main thread sits in the parallel_for below
typedef struct sv_fill_t { float x0, z0, step; } sv_fill_t;
static void sv_fill_rows(void *arg, int begin, int end, int slot) {
    const sv_fill_t *f = (const sv_fill_t *)arg; (void)slot;
    for (int j = begin; j < end; ++j) for (int i = 0; i < SV_RES; ++i)
        sv_buf[j * SV_RES + i] = stream_delta_at(f->x0 + i * f->step, f->z0 + j * f->step);
}

static void sv_sync_authored(void) {
    char name[64];
    sv_tiles_unmodeled = 0;
    for (int i = 0; i < stream_tile_count(); ++i) {
        stream_tile_t *t = stream_tile_at_index(i);
        if (t->level < STREAM_L_COASTAL || t->water_only) continue;
        snprintf(name, sizeof name, "tile_l%d_%d_%d", t->level, t->tx, t->ty);
        stream_set_authored(t->level, t->tx, t->ty, asset_find(name) != NULL);
    }
}

void streamview_update(void) {
    if (!sv_inited) {
        sv_inited = 1;
        sv_buf = malloc((size_t)SV_RES * SV_RES * sizeof(float));
        sv_tex = rhi_texture_make(SV_RES, SV_RES, RHI_FMT_R32F, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
        const char *e = getenv("MARE_STREAM_TILES"); if (e) stream_show_tiles = atoi(e) != 0;   // overlay on for unattended captures
    }
    if (sv_world_seed != G.seed) {   // new game or a loaded save: detail is keyed to the world seed
        sv_world_seed = G.seed;
        stream_init(G.seed, "tiles");
        sv_on = 0; sv_seen_residency = 0; sv_seen_assets = (unsigned)-1;
    }
    stream_update(cam.tx, cam.tz, cam.dist);
    if (sv_seen_assets != asset_generation()) { sv_seen_assets = asset_generation(); sv_sync_authored(); }
    sv_tiles_unmodeled = 0;   // wanted land leaves without an authored mesh: the "still to model" list
    for (int i = 0; i < stream_tile_count(); ++i) { const stream_tile_t *t = stream_tile_at_index(i);
        sv_tiles_unmodeled += t->wanted && !t->water_only && !t->authored && t->level == STREAM_L_TACTICAL; }

    // rebuild the window when residency changed or the camera left the inner half
    float cx = cam.tx, cz = cam.tz;
    int moved = !sv_on || fabsf(cx - (sv_x0 + SV_EXTENT * 0.5f)) > SV_EXTENT * 0.25f
                       || fabsf(cz - (sv_z0 + SV_EXTENT * 0.5f)) > SV_EXTENT * 0.25f;
    int changed = sv_seen_residency != stream_residency_generation();
    if ((moved || changed) && RF.time - sv_last_compose > SV_MIN_RECOMPOSE_S) {
        sv_last_compose = RF.time;
        sv_seen_residency = stream_residency_generation();
        double t0 = diag_now();
        float step = SV_EXTENT / (SV_RES - 1);
        sv_fill_t f;                                 // snapped to the sample grid so detail never swims
        f.x0 = floorf((cx - SV_EXTENT * 0.5f) / step) * step;
        f.z0 = floorf((cz - SV_EXTENT * 0.5f) / step) * step;
        f.step = step;
        jobs_parallel_for(JOB_LANE_WORKER, sv_fill_rows, &f, SV_RES, 32);
        rhi_texture_update(&sv_tex, sv_buf);
        sv_x0 = f.x0; sv_z0 = f.z0; sv_on = 1;
        sv_window_ms = (diag_now() - t0) * 1000;
    }

    // metrics (task 7)
    static int m_res = -1, m_want, m_load, m_mb, m_evict, m_disk, m_gen, m_win, m_unmod;
    if (m_res < 0) { m_res = metric_id("stream.resident"); m_want = metric_id("stream.wanted"); m_load = metric_id("stream.loading");
                     m_mb = metric_id("stream.mb"); m_evict = metric_id("stream.evicted"); m_disk = metric_id("stream.disk_hits");
                     m_gen = metric_id("stream.generated"); m_win = metric_id("stream.window_ms"); m_unmod = metric_id("stream.unmodeled"); }
    stream_stats_t s; stream_stats(&s);
    metric_set(m_res, s.resident); metric_set(m_want, s.wanted); metric_set(m_load, s.loading);
    metric_set(m_mb, s.bytes / (1024.0 * 1024.0)); metric_set(m_evict, s.evicted);
    metric_set(m_disk, s.disk_hits); metric_set(m_gen, s.generated); metric_set(m_win, sv_window_ms);
    metric_set(m_unmod, sv_tiles_unmodeled);
}

// terrain.c binds these: the window texture and (x0, z0, 1/extent, 1/extent)
const rhi_texture_t *streamview_detail_tex(void) { return sv_on ? &sv_tex : 0; }
void streamview_detail_rect(float out[4]) { out[0] = sv_x0; out[1] = sv_z0; out[2] = 1.f / SV_EXTENT; out[3] = 1.f / SV_EXTENT; }
double streamview_window_ms(void) { return sv_window_ms; }
int    streamview_unmodeled(void) { return sv_tiles_unmodeled; }

// Authored terrain tiles (`tile_l<level>_<tx>_<ty>` assets): drawn through the normal asset
// path. Called inside the scene_assets_begin/end bracket (scene_build_ports).
void streamview_push_tiles(void) {
    if (cam.mapness > 0.5f) return;
    for (int i = 0; i < stream_tile_count(); ++i) {
        stream_tile_t *t = stream_tile_at_index(i);
        if (!t->authored || !t->wanted || t->level < STREAM_L_COASTAL) continue;
        float x0, z0, x1, z1; stream_tile_rect(t, &x0, &z0, &x1, &z1);
        float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        float dd = dist2d(cx, cz, cam.pos.x, cam.pos.z), d = sqrtf(dd * dd + cam.pos.y * cam.pos.y);
        if (d > (x1 - x0) * 3.f) continue;
        char name[64]; snprintf(name, sizeof name, "tile_l%d_%d_%d", t->level, t->tx, t->ty);
        asset_t *a = asset_find(name);
        if (!a) continue;
        if (a->state == ASSET_UNLOADED) asset_request(a);
        if (a->state != ASSET_LOADED) continue;
        inst_t in = { cx, 0, cz, 0, 1.f, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
        scene_asset_instance(a, in, lod_key(LODK_MISC, i, 1), d);
    }
}

// Debug overlay: the wanted set as frames at the tile bounds - green resident (bright when an
// authored mesh exists), yellow loading, blue on disk, red missing; wanted land leaves without
// an authored mesh get a diagonal cross: the grids still to model, shown in place.
void streamview_draw_debug(void) {
    if (!stream_show_tiles || cam.mapness > 0.95f) return;
    for (int i = 0; i < stream_tile_count(); ++i) {
        const stream_tile_t *t = stream_tile_at_index(i);
        if (!t->wanted || t->water_only) continue;
        float x0, z0, x1, z1; stream_tile_rect(t, &x0, &z0, &x1, &z1);
        float lift = 0.4f + 0.15f * (STREAM_LEVELS - 1 - t->level);
        unsigned col = t->state == TILE_READY  ? (t->authored ? rgb(90, 255, 120) : rgb(60, 170, 80))
                     : t->state == TILE_QUEUED ? rgb(230, 200, 60)
                     : t->on_disk              ? rgb(80, 130, 230) : rgb(210, 70, 50);
        unsigned c = rgb_linear(col);
        // each edge in segments that follow the ground, so frames read on hills and over water alike
        int segs = t->level >= STREAM_L_COASTAL ? 8 : 4;
        static const int ex[5] = { 0, 1, 1, 0, 0 }, ez[5] = { 0, 0, 1, 1, 0 };   // corner ring
        for (int e = 0; e < 4; ++e) {
            float ax = ex[e] ? x1 : x0, az = ez[e] ? z1 : z0;
            float bx = ex[e+1] ? x1 : x0, bz = ez[e+1] ? z1 : z0;
            float px = ax, pz = az, py = maxf(world_height(ax, az), 0.f) + lift;
            for (int s = 1; s <= segs; ++s) {
                float qx = ax + (bx - ax) * s / segs, qz = az + (bz - az) * s / segs;
                float qy = maxf(world_height(qx, qz), 0.f) + lift;
                dd_line(vec3(px, py, pz), vec3(qx, qy, qz), .color = c);
                px = qx; py = qy; pz = qz;
            }
        }
        if (t->level == STREAM_L_TACTICAL && !t->authored) {   // a placeholder where a mesh could go
            unsigned m = rgb_linear(rgb(200, 90, 60));
            float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f, cy = maxf(world_height(cx, cz), 0.f) + lift;
            dd_line(vec3(x0, maxf(world_height(x0, z0), 0.f) + lift, z0), vec3(cx, cy, cz), .color = m);
            dd_line(vec3(x1, maxf(world_height(x1, z0), 0.f) + lift, z0), vec3(cx, cy, cz), .color = m);
            dd_line(vec3(x1, maxf(world_height(x1, z1), 0.f) + lift, z1), vec3(cx, cy, cz), .color = m);
            dd_line(vec3(x0, maxf(world_height(x0, z1), 0.f) + lift, z1), vec3(cx, cy, cz), .color = m);
        }
    }
}
