// mare::engine::render::StreamView (see render.h): streaming view (game/docs/DESIGN_STREAMING.md
// tasks 3 and 7): feeds the world streamer with
// the camera, composes the finest resident tiles into one R32F "detail window" the terrain
// shader adds on top of the base heightmap, marks tiles that have authored meshes, draws the
// tile grid as a debug overlay (the list of grids still to model, in place, in the world), and
// publishes the streaming metrics.
//
// The window is composed with jobs_parallel_for on the WORKER lane from the main thread, so it
// never races a tile eviction (evictions only happen inside stream_update, also on the main
// thread); at 512x512 it costs well under a millisecond across the pool and is only rebuilt
// when the residency changes or the camera leaves the inner half of the window.
#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdlib>
#include <cmath>

extern "C" {
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
#include "../world/geochunks.h"
#include "heightatlas.h"
#include "../assets/asset.h"
}

#define SV_RES     512
#define SV_MAX_SLOTS 64         // worker slots the tally array covers (jobs_slot_count())
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

namespace mare::engine::render {

class StreamView {
public:
    void update();
    void push_tiles();
    void draw_debug();
    const rhi_texture_t *detail_tex(int ring);
    void   detail_rect(int ring, float out[4]);
    float  detail_texel(int ring);
    int    rings() { return SV_RINGS; }
    double window_ms() { return sv_window_ms; }
    int    unmodeled() { return sv_tiles_unmodeled; }

private:
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
    sv_ring_t sv_ring[SV_RINGS];

    int    sv_inited = 0;
    unsigned sv_seen_assets = 0;
    unsigned sv_world_seed = 0xFFFFFFFFu;
    double sv_window_ms = 0;          // both rings together, last rebuild
    int    sv_tiles_unmodeled = 0;    // wanted land leaves without an authored mesh (dashboard)

    // one row band of a ring; reads tile payloads which only ever appear (never vanish) while the
    // main thread sits in the parallel_for below
    typedef struct sv_fill_t { float *buf; float x0, z0, step; int max_level;
                               float port_xz[96][2]; int nports;
                               int src[SV_MAX_SLOTS][3]; } sv_fill_t;   // which source answered, per worker slot (no atomics: the bands are disjoint)   // the harbours inside the ring (coast polish keeps clear of them)
    static void sv_fill_rows(void *arg, int begin, int end, int slot);
    double sv_ring_compose(sv_ring_t *r, float cx, float cz);
    void sv_sync_authored();
};

StreamView &stream_view() { static StreamView s; return s; }

// Coast polish (0.19). The base heightfield is a 16-cells-per-degree raster of the coastline
// polygons, so at battle zoom its waterline reads as 7 km steps. Inside a narrow band around
// that waterline the ring may now *move the shore*: where the authored tiles are resident
// their own heights pass unclamped (GEBCO's coast at 520 m or 3 km instead of the raster's),
// elsewhere the raster corners are rounded with a half-cell box blur and a fine fbm wiggles
// the contour at a sub-cell scale. The band is a cell or so wide and fades out at the
// harbours, so no port town ends up in the sea and no shipping lane silts up: the simulation
// never reads the rings, this is presentation only.
#define SV_COAST_BAND   0.62f    // |base height| the shore work reaches to (the first land cell and the first sea cell)
#define SV_COAST_CORE   0.22f    // ... and is at full strength under (small islands are lifted to 0.55 and stay as authored)
#define SV_PORT_KEEP    1.6f     // world units around a harbour the shoreline stays as placed
#define SV_PORT_FADE    3.2f     // ... fading back to the polished shore by here
// The coastal fringe (cells of coast distance) where the authored relief *replaces* the
// procedural one instead of only raising it: the 0.17 cliff field lifts the first inland
// cells into a wall wherever it is high, and GEBCO knows which coasts are really cliffs.
#define SV_FRINGE_IN    1.5f
#define SV_FRINGE_OUT   5.0f

void StreamView::sv_fill_rows(void *arg, int begin, int end, int slot) {
    sv_fill_t *f = (sv_fill_t *)arg;
    const float cell = WORLD_K / CELLS_PER_DEG;
    int src_fine = 0, src_chunk = 0, src_cut = 0;   // which source answered, tallied per band
    for (int j = begin; j < end; ++j) for (int i = 0; i < SV_RES; ++i) {
        float wx = f->x0 + i * f->step, wz = f->z0 + j * f->step, h;
        float lon = x2lon(wx), lat = z2lat(wz);
        float base = world_height(wx, wz);
        float sd = stream_delta_at_level(wx, wz, f->max_level);
        // The authored Earth where its tile is resident - the delta is what it says the ground
        // should be, minus what the base map already draws - and the procedural detail
        // elsewhere, so a ring is never empty while tiles are still coming in. The authored map
        // is a loose Earth (its coastline disagrees with the base in places), so the delta is
        // clamped to the base's side of the waterline: relief and bathymetry, never a landform
        // rising through the sea or a hole opening in a coast the ports were placed against.
        // Three authored sources, best first: the fine regional cut (~520 m), then the GEBCO
        // chunks read exactly as exported (float32 metres, assets/data/how_to.txt), then the
        // u16 global cut of that same export. All three decode to the same world units, so
        // which one answers only changes the fidelity, never the calibration.
        int fine = 0;
        if      (terraintiles_height_fine(lon, lat, &h)) { fine = 1; src_fine++; }
        else if (geochunks_height(lon, lat, &h))         { fine = 1; src_chunk++; }
        else if (terraintiles_height(lon, lat, &h))      { fine = 1; src_cut++; }
        float d;
        if (fine) {
            // the authored map's broad relief carries the synthesized ridges on top (0.17): the
            // source's land band is compressed and smooth, the ridges are what makes it a range
            // and, as in the bake, the authored map may only raise land and only deepen sea: a
            // loose coastline neither flattens a hill nor lifts a sea floor to the surface
            d = (base > 0.f ? maxf(h, base) : minf(h, base)) - base + 0.7f * sd;
        } else
            d = sd;
        // how far this texel is from a harbour: 0 at the quay, 1 beyond SV_PORT_FADE
        float portw = 1.f;
        for (int p = 0; p < f->nports && portw > 0.f; ++p) {
            float dp = dist2d(wx, wz, f->port_xz[p][0], f->port_xz[p][1]);
            float u = (dp - SV_PORT_KEEP) / (SV_PORT_FADE - SV_PORT_KEEP);
            portw *= u <= 0.f ? 0.f : u >= 1.f ? 1.f : u * u * (3.f - 2.f * u);
        }
        // The authored heights on the shore side, as the sea shading can take them: the sea's
        // shallow-water language (surf band, lapping foam, turquoise) was tuned for a floor that
        // starts at the base's -0.30 in the first sea cell, and GEBCO's real 2-30 m shallows
        // sit well above that - taken raw they turn every bay white. So a base sea cell may only
        // deepen, a base land cell keeps a beach above the sea's land threshold, and the
        // shoreline itself moves only where the authored map is decisive (land over 0.05,
        // sea under -0.05).
        float h_sea = minf(h, -0.30f), h_land = maxf(h, 0.20f);
        // the coastal fringe: the real relief in place of the procedural cliff wall, on its own
        // side of the waterline (the band below is the only place the shoreline itself moves).
        // The coast distance is bilinear over the cells so the fringe has no cell steps.
        if (fine && portw > 0.001f) {
            float cxf = (lon - LON0) * CELLS_PER_DEG - 0.5f, cyf = (LAT1 - lat) * CELLS_PER_DEG - 0.5f;
            int cx0 = (int)floorf(cxf), cy0 = (int)floorf(cyf);
            if (cx0 >= 0 && cy0 >= 0 && cx0 + 1 < LM_W && cy0 + 1 < LM_H) {
                float tx = cxf - cx0, ty = cyf - cy0;
                float d00 = (float)abs((int)coastdist[cy0 * LM_W + cx0]),     d10 = (float)abs((int)coastdist[cy0 * LM_W + cx0 + 1]);
                float d01 = (float)abs((int)coastdist[(cy0 + 1) * LM_W + cx0]), d11 = (float)abs((int)coastdist[(cy0 + 1) * LM_W + cx0 + 1]);
                float dc = lerpf(lerpf(d00, d10, tx), lerpf(d01, d11, tx), ty);
                float u = (dc - SV_FRINGE_IN) / (SV_FRINGE_OUT - SV_FRINGE_IN);
                float wc = (1.f - (u <= 0.f ? 0.f : u >= 1.f ? 1.f : u * u * (3.f - 2.f * u))) * portw;
                if (wc > 0.001f && base > 0.f) d = lerpf(d, h_land - base + 0.4f * sd, wc);   // land: GEBCO's slope instead of the wall
            }
        }
        // the shoreline band: see the note above. The sea side reaches further (the first sea
        // cell sits at -0.37, the second at -0.5): where the authored map says land there, the
        // mountain's foot continues into what the raster cut off as a vertical face.
        float ab = fabsf(base);
        float band = base < 0.f ? 0.78f : SV_COAST_BAND, core = base < 0.f ? 0.32f : SV_COAST_CORE;
        if (ab < band) {
            float t = (ab - core) / (band - core);
            float w = (1.f - (t <= 0.f ? 0.f : t >= 1.f ? 1.f : t * t * (3.f - 2.f * t))) * portw;
            if (w > 0.001f) {
                float hc = 0.5f * cell;
                float blur = 0.25f * (world_height(wx + hc, wz) + world_height(wx - hc, wz) + world_height(wx, wz + hc) + world_height(wx, wz - hc));
                // the wiggle's shortest wavelength is four ring texels: finer than that it
                // aliases into a sawtooth along the shore instead of a natural line
                float wf = minf(40.f, 1.f / (8.f * f->step / WORLD_K));
                float wig = (world_fbm(lon * wf + 3.f, lat * wf + 11.f, 2, 0x5A17u) - 0.5f) * (fine ? 0.08f : 0.22f);
                float target;
                if (fine) {
                    float hs = h > 0.05f ? h_land : h < -0.05f ? h_sea : (base > 0.f ? h_land : h_sea);   // decisive, else stay on the base's side
                    target = hs - base + wig;
                } else
                    target = (blur - base) + wig;
                d = lerpf(d, target, w);
            }
        }
        f->buf[j * SV_RES + i] = d;
    }
    // a band's own tally, folded in once (the rows are disjoint, so this is a plain add per
    // worker rather than a contended counter per texel)
    int k = (unsigned)slot < SV_MAX_SLOTS ? slot : 0;
    f->src[k][0] += src_fine; f->src[k][1] += src_chunk; f->src[k][2] += src_cut;
}

// Rebuild one ring when the residency changed or the camera left its inner half. Returns the
// milliseconds spent (0 when nothing had to be done).
double StreamView::sv_ring_compose(sv_ring_t *r, float cx, float cz) {
    int moved = !r->on || fabsf(cx - (r->x0 + r->extent * 0.5f)) > r->extent * 0.25f
                       || fabsf(cz - (r->z0 + r->extent * 0.5f)) > r->extent * 0.25f;
    unsigned resgen = stream_residency_generation() ^ (terraintiles_generation() * 2654435761u) ^ (geochunks_generation() * 2246822519u);
    int changed = r->seen_residency != resgen;
    if (!(moved || changed) || RF.time - r->last_compose <= SV_MIN_RECOMPOSE_S) return 0;
    r->last_compose = RF.time;
    r->seen_residency = resgen;
    double t0 = diag_now();
    float step = r->extent / (SV_RES - 1);
    sv_fill_t f;                                 // snapped to the sample grid so detail never swims
    f.buf = r->buf;
    f.x0 = floorf((cx - r->extent * 0.5f) / step) * step;
    f.z0 = floorf((cz - r->extent * 0.5f) / step) * step;
    f.step = step; f.max_level = r->max_level;
    memset(f.src, 0, sizeof f.src);
    f.nports = 0;   // every harbour the ring touches: the shoreline stays as placed around it
    for (int i = 0; i < G.nports && f.nports < 96; ++i) {
        const port_t *p = &G.ports[i];
        if (p->x < f.x0 - SV_PORT_FADE || p->x > f.x0 + r->extent + SV_PORT_FADE || p->z < f.z0 - SV_PORT_FADE || p->z > f.z0 + r->extent + SV_PORT_FADE) continue;
        f.port_xz[f.nports][0] = p->x; f.port_xz[f.nports][1] = p->z; f.nports++;
    }
    jobs_parallel_for(JOB_LANE_WORKER, sv_fill_rows, &f, SV_RES, 32);
    {   // what the ring holds: the range of the delta and how much of it the shore work touched
        float mn = 1e9f, mx = -1e9f; int big = 0;
        for (int i = 0; i < SV_RES * SV_RES; ++i) { float v = r->buf[i]; mn = minf(mn, v); mx = maxf(mx, v); big += v < -0.5f; }
        int sf = 0, sc = 0, su = 0;
        for (int k = 0; k < SV_MAX_SLOTS; ++k) { sf += f.src[k][0]; sc += f.src[k][1]; su += f.src[k][2]; }
        LOG_DEBUG("stream", "ring %s composed at %.1f,%.1f: delta %.2f..%.2f, %d texels lowered over 0.5, %d harbours; sources: %d fine, %d GEBCO chunk, %d u16 cut",
                  r->extent < 100.f ? "near" : "far", f.x0, f.z0, mn, mx, big, f.nports, sf, sc, su);
    }
    rhi_texture_update(&r->tex, r->buf);
    r->x0 = f.x0; r->z0 = f.z0; r->on = 1;
    return (diag_now() - t0) * 1000;
}

void StreamView::sv_sync_authored() {
    char name[64];
    sv_tiles_unmodeled = 0;
    for (int i = 0; i < stream_tile_count(); ++i) {
        stream_tile_t *t = stream_tile_at_index(i);
        if (t->level < STREAM_L_COASTAL || t->water_only) continue;
        snprintf(name, sizeof name, "tile_l%d_%d_%d", t->level, t->tx, t->ty);
        stream_set_authored(t->level, t->tx, t->ty, asset_find(name) != NULL);
    }
}

void StreamView::update() {
    if (!sv_inited) {
        sv_inited = 1;
        static const float extent[SV_RINGS] = { SV_NEAR_EXTENT, SV_FAR_EXTENT };
        static const int   lvl[SV_RINGS]    = { STREAM_L_TACTICAL, STREAM_L_COASTAL };
        for (int i = 0; i < SV_RINGS; ++i) {
            sv_ring[i].buf = (float *)malloc((size_t)SV_RES * SV_RES * sizeof(float));
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
    // A chunk is 13 degrees across, so one or two cover any view: a small radius is enough and
    // keeps the 1.3 MB reads rare.
    geochunks_update(x2lon(cam.tx), z2lat(cam.tz), clampf(cam.dist / WORLD_K * 0.5f + 1.f, 1.f, 14.f));
    heightatlas_update();
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
    static int m_gc_res = -1, m_gc_mb, m_gc_load, m_ha_slots;
    if (m_gc_res < 0) { m_gc_res = metric_id("terrain.chunks_resident"); m_gc_mb = metric_id("terrain.chunks_mb");
                        m_gc_load = metric_id("terrain.chunks_loading"); m_ha_slots = metric_id("terrain.atlas_slots"); }
    { gc_stats_t gs; geochunks_stats(&gs);
      metric_set(m_gc_res, gs.resident); metric_set(m_gc_mb, gs.bytes / (1024.0 * 1024.0));
      metric_set(m_gc_load, gs.loading); metric_set(m_ha_slots, heightatlas_resident()); }
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
const rhi_texture_t *StreamView::detail_tex(int ring) { return (unsigned)ring < SV_RINGS && sv_ring[ring].on ? &sv_ring[ring].tex : 0; }
void StreamView::detail_rect(int ring, float out[4]) {
    const sv_ring_t *r = &sv_ring[(unsigned)ring < SV_RINGS ? ring : 0];
    out[0] = r->x0; out[1] = r->z0; out[2] = 1.f / r->extent; out[3] = 1.f / r->extent;
}
float  StreamView::detail_texel(int ring) { return ((unsigned)ring < SV_RINGS ? sv_ring[ring].extent : SV_NEAR_EXTENT) / (SV_RES - 1); }

// Authored terrain tiles (`tile_l<level>_<tx>_<ty>` assets): drawn through the normal asset
// path. Called inside the scene_assets_begin/end bracket (scene_build_ports).
void StreamView::push_tiles() {
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
void StreamView::draw_debug() {
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
                dd_line(vec3(px, py, pz), vec3(qx, qy, qz), dd_opts().with_color(c));
                px = qx; py = qy; pz = qz;
            }
        }
        if (t->level == STREAM_L_TACTICAL && !t->authored) {   // a placeholder where a mesh could go
            unsigned m = rgb_linear(rgb(200, 90, 60));
            float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f, cy = maxf(world_height(cx, cz), 0.f) + lift;
            dd_line(vec3(x0, maxf(world_height(x0, z0), 0.f) + lift, z0), vec3(cx, cy, cz), dd_opts().with_color(m));
            dd_line(vec3(x1, maxf(world_height(x1, z0), 0.f) + lift, z0), vec3(cx, cy, cz), dd_opts().with_color(m));
            dd_line(vec3(x1, maxf(world_height(x1, z1), 0.f) + lift, z1), vec3(cx, cy, cz), dd_opts().with_color(m));
            dd_line(vec3(x0, maxf(world_height(x0, z1), 0.f) + lift, z1), vec3(cx, cy, cz), dd_opts().with_color(m));
        }
    }
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
void streamview_update(void) { mare::engine::render::stream_view().update(); }
void streamview_push_tiles(void) { mare::engine::render::stream_view().push_tiles(); }
void streamview_draw_debug(void) { mare::engine::render::stream_view().draw_debug(); }
const rhi_texture_t *streamview_detail_tex(int ring) { return mare::engine::render::stream_view().detail_tex(ring); }
void  streamview_detail_rect(int ring, float out[4]) { mare::engine::render::stream_view().detail_rect(ring, out); }
float streamview_detail_texel(int ring) { return mare::engine::render::stream_view().detail_texel(ring); }
int   streamview_rings(void) { return mare::engine::render::stream_view().rings(); }
double streamview_window_ms(void) { return mare::engine::render::stream_view().window_ms(); }
int   streamview_unmodeled(void) { return mare::engine::render::stream_view().unmodeled(); }
}
