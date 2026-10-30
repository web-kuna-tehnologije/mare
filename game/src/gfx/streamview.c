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
#include "../world/terraintiles.h"
#include "../assets/asset.h"

#define SV_RES     512
#define SV_MIN_RECOMPOSE_S 0.20     // residency bursts (a tile a frame) fold into one rebuild

// Two clipmap rings (DESIGN_STREAMING.md follow-up 3). The near window carries the tactical
// (L3) detail around the camera; the far ring carries the coastal (L2) detail out to the
// regional view, so pulling back no longer drops every bit of relief at the window's edge.
// Each extent is set by Nyquist against its level's detail frequency in stream.c: L3 runs at
// 6 cycles/degree (wavelength 0.67 world units, needs <= 0.33 per texel), L2 at 3 (1.33 units,
// needs <= 0.67). At 512 texels that is 80 and 320 world units.
#define SV_NEAR_EXTENT  80.f        // 0.156 world units per texel
#define SV_FAR_EXTENT  320.f        // 0.625 world units per texel
enum { SV_NEAR = 0, SV_FAR, SV_RINGS };

int stream_show_tiles;               // dashboard toggle: draw the tile grid overlay

typedef struct sv_ring_t {
    rhi_texture_t tex;
    float   *buf;
    float    extent;                 // world units covered
    int      max_level;              // finest tile level this ring may sample (Nyquist, above)
    float    x0, z0;                 // origin (world)
    int      on;
    unsigned seen_residency;
    double   last_compose;
} sv_ring_t;
static sv_ring_t sv_ring[SV_RINGS];

static int    sv_inited;
static unsigned sv_seen_assets;
static unsigned sv_world_seed = 0xFFFFFFFFu;
static double sv_window_ms;          // both rings together, last rebuild
static int    sv_tiles_unmodeled;    // wanted land leaves without an authored mesh (dashboard)

// one row band of a ring; reads tile payloads which only ever appear (never vanish) while the
// main thread sits in the parallel_for below
typedef struct sv_fill_t { float *buf; float x0, z0, step; int max_level; } sv_fill_t;
static void sv_fill_rows(void *arg, int begin, int end, int slot) {
    const sv_fill_t *f = (const sv_fill_t *)arg; (void)slot;
    for (int j = begin; j < end; ++j) for (int i = 0; i < SV_RES; ++i) {
        float wx = f->x0 + i * f->step, wz = f->z0 + j * f->step, h;
        // The authored Earth where its tile is resident - the delta is what it says the ground
        // should be, minus what the base map already draws - and the procedural detail
        // elsewhere, so a ring is never empty while tiles are still coming in. The authored map
        // is a loose Earth (its coastline disagrees with the base in places), so the delta is
        // clamped to the base's side of the waterline: relief and bathymetry, never a landform
        // rising through the sea or a hole opening in a coast the ports were placed against.
        if (terraintiles_height(x2lon(wx), z2lat(wz), &h)) {
            float base = world_height(wx, wz);
            // the authored map's broad relief carries the synthesized ridges on top (0.17): the
            // source's land band is compressed and smooth, the ridges are what makes it a range
            // and, as in the bake, the authored map may only raise land and only deepen sea: a
            // loose coastline neither flattens a hill nor lifts a sea floor to the surface
            f->buf[j * SV_RES + i] = (base > 0.f ? maxf(h, base) : minf(h, base)) - base
                                   + 0.7f * stream_delta_at_level(wx, wz, f->max_level);
        } else
            f->buf[j * SV_RES + i] = stream_delta_at_level(wx, wz, f->max_level);
    }
}

// Rebuild one ring when the residency changed or the camera left its inner half. Returns the
// milliseconds spent (0 when nothing had to be done).
static double sv_ring_compose(sv_ring_t *r, float cx, float cz) {
    int moved = !r->on || fabsf(cx - (r->x0 + r->extent * 0.5f)) > r->extent * 0.25f
                       || fabsf(cz - (r->z0 + r->extent * 0.5f)) > r->extent * 0.25f;
    int changed = r->seen_residency != (stream_residency_generation() ^ (terraintiles_generation() * 2654435761u));
    if (!(moved || changed) || RF.time - r->last_compose <= SV_MIN_RECOMPOSE_S) return 0;
    r->last_compose = RF.time;
    r->seen_residency = stream_residency_generation() ^ (terraintiles_generation() * 2654435761u);
    double t0 = diag_now();
    float step = r->extent / (SV_RES - 1);
    sv_fill_t f;                                 // snapped to the sample grid so detail never swims
    f.buf = r->buf;
    f.x0 = floorf((cx - r->extent * 0.5f) / step) * step;
    f.z0 = floorf((cz - r->extent * 0.5f) / step) * step;
    f.step = step; f.max_level = r->max_level;
    jobs_parallel_for(JOB_LANE_WORKER, sv_fill_rows, &f, SV_RES, 32);
    rhi_texture_update(&r->tex, r->buf);
    r->x0 = f.x0; r->z0 = f.z0; r->on = 1;
    return (diag_now() - t0) * 1000;
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
        static const float extent[SV_RINGS] = { SV_NEAR_EXTENT, SV_FAR_EXTENT };
        static const int   lvl[SV_RINGS]    = { STREAM_L_TACTICAL, STREAM_L_COASTAL };
        for (int i = 0; i < SV_RINGS; ++i) {
            sv_ring[i].buf = malloc((size_t)SV_RES * SV_RES * sizeof(float));
            sv_ring[i].tex = rhi_texture_make(SV_RES, SV_RES, RHI_FMT_R32F, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
            sv_ring[i].extent = extent[i]; sv_ring[i].max_level = lvl[i]; sv_ring[i].last_compose = -1e9;
        }
        const char *e = getenv("MARE_STREAM_TILES"); if (e) stream_show_tiles = atoi(e) != 0;   // overlay on for unattended captures
    }
    if (sv_world_seed != G.seed) {   // new game or a loaded save: detail is keyed to the world seed
        sv_world_seed = G.seed;
        stream_init(G.seed, "tiles");
        sv_seen_assets = (unsigned)-1;
        for (int i = 0; i < SV_RINGS; ++i) { sv_ring[i].on = 0; sv_ring[i].seen_residency = 0; }
    }
    stream_update(cam.tx, cam.tz, cam.dist);
    // Page the authored terrain around the look-at point. The radius follows the zoom, so a
    // city view holds a couple of tiles and the strategic map a band across the ocean.
    terraintiles_update(x2lon(cam.tx), z2lat(cam.tz),
                        clampf(cam.dist / WORLD_K * 0.9f + 2.f, 2.f, 40.f));
    if (sv_seen_assets != asset_generation()) { sv_seen_assets = asset_generation(); sv_sync_authored(); }
    sv_tiles_unmodeled = 0;   // wanted land leaves without an authored mesh: the "still to model" list
    for (int i = 0; i < stream_tile_count(); ++i) { const stream_tile_t *t = stream_tile_at_index(i);
        sv_tiles_unmodeled += t->wanted && !t->water_only && !t->authored && t->level == STREAM_L_TACTICAL; }

    // At most one ring is rebuilt per frame (the near one first: it is what the player is
    // looking at), so a residency change never pays for both composes in the same frame.
    { double ms = sv_ring_compose(&sv_ring[SV_NEAR], cam.tx, cam.tz);
      if (ms == 0) ms = sv_ring_compose(&sv_ring[SV_FAR], cam.tx, cam.tz);
      if (ms > 0) sv_window_ms = ms; }

    // metrics (task 7)
    static int m_tt_res = -1, m_tt_load, m_tt_mb, m_tt_ev;
    if (m_tt_res < 0) { m_tt_res = metric_id("terrain.tiles_resident"); m_tt_load = metric_id("terrain.tiles_loading");
                        m_tt_mb = metric_id("terrain.tiles_mb"); m_tt_ev = metric_id("terrain.tiles_evicted"); }
    { tt_stats_t ts; terraintiles_stats(&ts);
      metric_set(m_tt_res, ts.resident); metric_set(m_tt_load, ts.loading);
      metric_set(m_tt_mb, ts.bytes / (1024.0 * 1024.0)); metric_set(m_tt_ev, ts.evicted); }
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

// terrain.c binds these per ring: the texture, (x0, z0, 1/extent, 1/extent), and the world
// size of one texel (the shader differentiates the delta for its normals).
const rhi_texture_t *streamview_detail_tex(int ring) { return (unsigned)ring < SV_RINGS && sv_ring[ring].on ? &sv_ring[ring].tex : 0; }
void streamview_detail_rect(int ring, float out[4]) {
    const sv_ring_t *r = &sv_ring[(unsigned)ring < SV_RINGS ? ring : 0];
    out[0] = r->x0; out[1] = r->z0; out[2] = 1.f / r->extent; out[3] = 1.f / r->extent;
}
float  streamview_detail_texel(int ring) { return ((unsigned)ring < SV_RINGS ? sv_ring[ring].extent : SV_NEAR_EXTENT) / (SV_RES - 1); }
int    streamview_rings(void) { return SV_RINGS; }
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
