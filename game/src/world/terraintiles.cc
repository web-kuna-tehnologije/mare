// Streamed terrain tiles (see terraintiles.h). Sections: the tile index · the load pipeline on
// the job system · the wanted set and the budget · sampling.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "terraintiles.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../assets/asset.h"
}

enum { TT_HEIGHT = 0, TT_HEIGHT_FINE, TT_NORMAL, TT_KINDS };   // FINE: the regional detail cut (height_fine_*)
typedef struct tt_job_t { tt_set_t *s; tt_tile_t *t; void *data; size_t bytes; double t0, ms; } tt_job_t;

namespace mare::game::world {

class TerrainTiles {
public:
    int      init(const char *dir);
    void     shutdown();
    void     update(float lon, float lat, float radius_deg);
    int      height(float lon, float lat, float *out);
    int      normal(float lon, float lat, float out[3]);
    int      resident(float lon, float lat);
    int      fine_info(tt_setinfo_t *out);
    const void *fine_tile(int tx, int ty);
    int      height_fine(float lon, float lat, float *out);
    static float decode_u16(unsigned short v) { return tt_h_of(v / 65535.f); }
    void     stats(tt_stats_t *out);
    unsigned generation() { return tt_gen; }
    int      bake(const char *dir, float *hm, unsigned char *lm, int w, int h,
                  float lon0, float lat1, float cells_per_deg,
                  const unsigned char *protect, int *flips);

private:
    tt_set_t  sets[TT_KINDS] = {};
    size_t    tt_bytes = 0, tt_budget = (size_t)TT_BUDGET_MB * 1024 * 1024;
    unsigned  tt_update_no = 0, tt_gen = 1;
    int       tt_loaded_total = 0, tt_evicted = 0, tt_inited = 0;
    double    tt_last_read_ms = 0;

    static int tt_json_num(const char *s, const char *key, double *out);
    static int tt_set_load(tt_set_t *s, const char *dir, const char *kind);
    static const char *tt_default_dir(char *buf, size_t n);
    void        tt_publish(void *p);
    static void tt_publish_cb(void *p);   // jobs_submit entry point: forwards to the singleton
    static void tt_read(void *p);
    static void tt_request(tt_set_t *s, tt_tile_t *t);
    static int  tt_index(const tt_set_t *s, float lon, float lat, int *tx, int *ty, float *fx, float *fy);
    static int  tt_sample_u16(const tt_set_t *s, float lon, float lat, float *out);
    static float tt_h_of(float g);
    static int  tt_band_sample(unsigned short **row, const tt_set_t *s, int ty, float u, float v, float *out);
};

TerrainTiles &terraintiles() { static TerrainTiles t; return t; }

// Sea level in the 16-bit heightfield, and the relief either side of it.
//
// The tiles are cut from the real GEBCO export (assets/data/export-global) by
// tools/terrain_from_gebco.py, which *defines* the encoding instead of the old AI map's
// measured one: 0.5 is exactly sea level, the land half spans 0..9000 m linearly, and the
// sea half 0..-12000 m sqrt-compressed (the cutter explains why: a linear real-metre shelf
// put the whole north Adriatic at ~0.04 units and washed every coastal sea into the shallow
// colour). Decoded through the linear scales below: Mont Blanc (3600 m at the export's 3 km
// sampling) ~10 units, the Dinaric crest ~5.6; the -70 m shelf ~0.7 units down, the Ionian
// deep ~4.8 - the band the shallows gate and the sea-floor darkening were tuned for. The old
// AI-map cut is kept in terrain/tiles-aimap; MARE_TERRAIN_DIR points at any cut for A/B runs.
#define TT_SEA           0.500f
#define TT_HEIGHT_SCALE  20.0f      // world units for the full 9000 m land range (0.19: lowered from 28 - less vertical exaggeration, same relative detail)
#define TT_DEPTH_SCALE    9.0f      // world units for the full 12000 m depth range

// ---------------------------------------------------------------------------
// index

int TerrainTiles::tt_json_num(const char *s, const char *key, double *out) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    *out = atof(p + 1); return 1;
}

int TerrainTiles::tt_set_load(tt_set_t *s, const char *dir, const char *kind) {
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    snprintf(s->kind, sizeof s->kind, "%s", kind);
    char path[320]; snprintf(path, sizeof path, "%s/%s.json", dir, kind);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG_DEBUG("terrain", "no %s", path); return 0; }
    char buf[2048]; size_t n = fread(buf, 1, sizeof buf - 1, f); buf[n] = 0; fclose(f);
    double v;
    if (!tt_json_num(buf, "tiles_x", &v)) return 0; s->tiles_x = (int)v;
    if (!tt_json_num(buf, "tiles_y", &v)) return 0; s->tiles_y = (int)v;
    if (tt_json_num(buf, "bytes_per_sample", &v)) s->bytes_per_sample = (int)v;
    if (tt_json_num(buf, "lon0", &v)) s->lon0 = (float)v;
    if (tt_json_num(buf, "lat0", &v)) s->lat0 = (float)v;
    if (tt_json_num(buf, "lon1", &v)) s->lon1 = (float)v;
    if (tt_json_num(buf, "lat1", &v)) s->lat1 = (float)v;
    if (tt_json_num(buf, "width", &v))  s->width  = (int)v;
    if (tt_json_num(buf, "height", &v)) s->height = (int)v;
    s->ntiles = s->tiles_x * s->tiles_y;
    if (s->ntiles <= 0 || s->ntiles > TT_MAX_TILES) { LOG_WARN("terrain", "%s: %d tiles is out of range", path, s->ntiles); return 0; }
    s->tile = (tt_tile_t *)calloc((size_t)s->ntiles, sizeof *s->tile);
    for (int ty = 0; ty < s->tiles_y; ++ty) for (int tx = 0; tx < s->tiles_x; ++tx) {
        tt_tile_t *t = &s->tile[ty * s->tiles_x + tx];
        t->tx = (short)tx; t->ty = (short)ty; t->kind = (unsigned char)(strcmp(kind, "normal") == 0);
    }
    s->present = 1;
    LOG_INFO("terrain", "%s tiles: %dx%d of %d (%d samples, %d bytes each), lon %.0f..%.0f lat %.0f..%.0f",
             kind, s->tiles_x, s->tiles_y, TT_TILE, s->width, s->bytes_per_sample, s->lon0, s->lon1, s->lat0, s->lat1);
    return 1;
}

// The default tile location: inside this worktree's asset tree first ("<assets>/terrain/tiles",
// which assets/elm carries), then the shared "<assets>/../terrain/tiles". Two sessions cutting
// tiles into one shared folder have already overwritten each other's index once; the per-tree
// copy is the one nothing else touches.
const char *TerrainTiles::tt_default_dir(char *buf, size_t n) {
    snprintf(buf, n, "%s/terrain/tiles", asset_root());
    char probe[384]; snprintf(probe, sizeof probe, "%s/height.json", buf);
    FILE *f = fopen(probe, "rb");
    if (f) { fclose(f); return buf; }
    snprintf(buf, n, "%s/../terrain/tiles", asset_root());
    return buf;
}

int TerrainTiles::init(const char *dir) {
    char fallback[320];
    // On by default: the tiles supply the real coastline (every Adriatic and Aegean island) and
    // the relief and bathymetry on top of the procedural base. The land band of the source is
    // compressed (see the calibration note above), so authored *relief* is gentle - the fine
    // detail comes from the normal tiles - but the geography is the thing the procedural world
    // cannot invent. MARE_TERRAIN_TILES=0 brings the old purely procedural world back.
    { const char *on = getenv("MARE_TERRAIN_TILES");
      if (on && atoi(on) == 0) { LOG_INFO("terrain", "streamed terrain tiles off (MARE_TERRAIN_TILES=0)"); return 0; } }
    if (!dir) dir = getenv("MARE_TERRAIN_DIR");           // A/B: point at any other tile cut
    if (!dir) dir = tt_default_dir(fallback, sizeof fallback);
    shutdown();
    const char *e = getenv("MARE_TERRAIN_MB");
    if (e && atoi(e) > 0) tt_budget = (size_t)atoi(e) * 1024 * 1024;
    int ok = tt_set_load(&sets[TT_HEIGHT], dir, "height");
    tt_set_load(&sets[TT_HEIGHT_FINE], dir, "height_fine");   // the Mediterranean 500 m cut, where present
    tt_set_load(&sets[TT_NORMAL], dir, "normal");
    tt_inited = ok;
    if (!ok) LOG_INFO("terrain", "no streamed terrain under %s: the procedural world stands", dir);
    else     LOG_INFO("terrain", "streamed terrain ready, budget %zu MB", tt_budget / (1024 * 1024));
    return ok;
}

void TerrainTiles::shutdown() {
    for (int k = 0; k < TT_KINDS; ++k) {
        tt_set_t *s = &sets[k];
        if (!s->tile) { memset(s, 0, sizeof *s); continue; }
        for (;;) { int busy = 0; for (int i = 0; i < s->ntiles; ++i) busy += s->tile[i].state == TT_QUEUED;
                   if (!busy) break; jobs_help(); }
        for (int i = 0; i < s->ntiles; ++i) free(s->tile[i].data);
        free(s->tile); memset(s, 0, sizeof *s);
    }
    tt_bytes = 0; tt_inited = 0;
}

// ---------------------------------------------------------------------------
// load pipeline: IO read -> MAIN publish

void TerrainTiles::tt_publish_cb(void *p) { terraintiles().tt_publish(p); }

void TerrainTiles::tt_publish(void *p) {          // MAIN
    tt_job_t *j = (tt_job_t *)p;
    if (j->data) { j->t->data = j->data; j->t->state = TT_READY; tt_bytes += j->bytes;
                   tt_loaded_total++; tt_last_read_ms = j->ms; tt_gen++; }
    else         { j->t->state = TT_EMPTY; }
    free(j);
}

void TerrainTiles::tt_read(void *p) {            // IO
    tt_job_t *j = (tt_job_t *)p;
    char path[384];
    snprintf(path, sizeof path, "%s/%s_%d_%d.bin", j->s->dir, j->s->kind, j->t->tx, j->t->ty);
    double t0 = diag_now();
    FILE *f = fopen(path, "rb");
    if (f) {
        void *d = malloc(j->bytes);
        if (d && fread(d, 1, j->bytes, f) == j->bytes) j->data = d; else free(d);
        fclose(f);
    }
    j->ms = (diag_now() - t0) * 1000;
    jobs_submit(JOB_LANE_MAIN, tt_publish_cb, j, 0);
}

void TerrainTiles::tt_request(tt_set_t *s, tt_tile_t *t) {
    if (t->state != TT_EMPTY) return;
    tt_job_t *j = (tt_job_t *)calloc(1, sizeof *j);
    j->s = s; j->t = t; j->bytes = (size_t)TT_TILE * TT_TILE * s->bytes_per_sample;
    t->state = TT_QUEUED;
    jobs_submit(JOB_LANE_IO, tt_read, j, 0);
}

// ---------------------------------------------------------------------------
// wanted set and budget

int TerrainTiles::tt_index(const tt_set_t *s, float lon, float lat, int *tx, int *ty, float *fx, float *fy) {
    if (!s->present || lon < s->lon0 || lon > s->lon1 || lat < s->lat0 || lat > s->lat1) return 0;
    float u = (lon - s->lon0) / (s->lon1 - s->lon0) * (s->width  - 1);
    float v = (s->lat1 - lat) / (s->lat1 - s->lat0) * (s->height - 1);
    int X = (int)u, Y = (int)v;
    *tx = X / TT_TILE; *ty = Y / TT_TILE;
    if (*tx < 0 || *ty < 0 || *tx >= s->tiles_x || *ty >= s->tiles_y) return 0;
    *fx = u - *tx * TT_TILE; *fy = v - *ty * TT_TILE;
    return 1;
}

void TerrainTiles::update(float lon, float lat, float radius_deg) {
    if (!tt_inited) return;
    tt_update_no++;
    int started = 0;
    for (int k = 0; k < TT_KINDS; ++k) {
        tt_set_t *s = &sets[k];
        if (!s->present) continue;
        int x0, y0, x1, y1; float fx, fy;
        if (!tt_index(s, clampf(lon - radius_deg, s->lon0, s->lon1), clampf(lat + radius_deg, s->lat0, s->lat1), &x0, &y0, &fx, &fy)) { x0 = y0 = 0; }
        if (!tt_index(s, clampf(lon + radius_deg, s->lon0, s->lon1), clampf(lat - radius_deg, s->lat0, s->lat1), &x1, &y1, &fx, &fy)) { x1 = s->tiles_x - 1; y1 = s->tiles_y - 1; }
        for (int ty = y0; ty <= y1; ++ty) for (int tx = x0; tx <= x1; ++tx) {
            tt_tile_t *t = &s->tile[ty * s->tiles_x + tx];
            t->last_want = tt_update_no;
            if (t->state == TT_EMPTY && started < TT_MAX_INFLIGHT) { tt_request(s, t); started++; }
        }
    }
    // budget: the coldest resident tile outside the wanted set goes first
    for (int guard = 0; guard < 8 && tt_bytes > tt_budget; ++guard) {
        tt_tile_t *victim = NULL; tt_set_t *vs = NULL;
        for (int k = 0; k < TT_KINDS; ++k) {
            tt_set_t *s = &sets[k]; if (!s->present) continue;
            for (int i = 0; i < s->ntiles; ++i) {
                tt_tile_t *t = &s->tile[i];
                if (t->state != TT_READY || t->last_want == tt_update_no) continue;
                if (!victim || t->last_want < victim->last_want) { victim = t; vs = s; }
            }
        }
        if (!victim) break;
        free(victim->data); victim->data = NULL; victim->state = TT_EMPTY;
        tt_bytes -= (size_t)TT_TILE * TT_TILE * vs->bytes_per_sample;
        tt_evicted++; tt_gen++;
    }
}

// ---------------------------------------------------------------------------
// sampling

int TerrainTiles::tt_sample_u16(const tt_set_t *s, float lon, float lat, float *out) {
    int tx, ty; float fx, fy;
    if (!tt_index(s, lon, lat, &tx, &ty, &fx, &fy)) return 0;
    const tt_tile_t *t = &s->tile[ty * s->tiles_x + tx];
    if (t->state != TT_READY) return 0;
    const unsigned short *d = (const unsigned short *)t->data;
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = mini(x0 + 1, TT_TILE - 1), y1 = mini(y0 + 1, TT_TILE - 1);
    float tx_ = fx - x0, ty_ = fy - y0;
    float a = d[y0 * TT_TILE + x0], b = d[y0 * TT_TILE + x1];
    float c = d[y1 * TT_TILE + x0], e = d[y1 * TT_TILE + x1];
    *out = lerpf(lerpf(a, b, tx_), lerpf(c, e, tx_), ty_) / 65535.f;
    return 1;
}

int TerrainTiles::height(float lon, float lat, float *out) {
    float g;
    // the fine regional set first (resident tiles only - tt_sample_u16 refuses the rest),
    // the global base as the fallback: detail where it exists, the whole world either way
    if (!tt_sample_u16(&sets[TT_HEIGHT_FINE], lon, lat, &g)
        && !tt_sample_u16(&sets[TT_HEIGHT], lon, lat, &g)) return 0;
    *out = g >= TT_SEA ? (g - TT_SEA) / (1.f - TT_SEA) * TT_HEIGHT_SCALE
                       : -(TT_SEA - g) / TT_SEA * TT_DEPTH_SCALE;
    return 1;
}

int TerrainTiles::normal(float lon, float lat, float out[3]) {
    const tt_set_t *s = &sets[TT_NORMAL];
    int tx, ty; float fx, fy;
    if (!tt_index(s, lon, lat, &tx, &ty, &fx, &fy)) return 0;
    const tt_tile_t *t = &s->tile[ty * s->tiles_x + tx];
    if (t->state != TT_READY) return 0;
    const unsigned char *d = (const unsigned char *)t->data;
    int x = mini((int)fx, TT_TILE - 1), y = mini((int)fy, TT_TILE - 1);
    const unsigned char *p = d + ((size_t)y * TT_TILE + x) * 3;
    float nx = p[0] / 127.5f - 1.f, ny = p[1] / 127.5f - 1.f, nz = p[2] / 127.5f - 1.f;
    float wx = nx, wy = nz, wz = -ny;               // tangent space: +x east, +y north, +z up
    float l = sqrtf(wx*wx + wy*wy + wz*wz);
    if (l < 1e-4f) return 0;
    out[0] = wx / l; out[1] = wy / l; out[2] = wz / l;
    return 1;
}


// ---------------------------------------------------------------------------
// the one-shot geography bake (see terraintiles.h)

float TerrainTiles::tt_h_of(float g) {
    return g >= TT_SEA ? (g - TT_SEA) / (1.f - TT_SEA) * TT_HEIGHT_SCALE
                       : -(TT_SEA - g) / TT_SEA * TT_DEPTH_SCALE;
}

// bilinear u16 sample from one row band of tiles; v is clamped into the band (the supersample
// offsets reach at most two source samples past a cell, a band is 512)
int TerrainTiles::tt_band_sample(unsigned short **row, const tt_set_t *s, int ty, float u, float v, float *out) {
    float v0 = (float)ty * TT_TILE, v1 = (float)(ty + 1) * TT_TILE - 1.001f;
    v = clampf(v, v0, v1) - v0;
    u = clampf(u, 0.f, (float)s->width - 1.001f);
    int X = (int)u, Y = (int)v;
    float fx = u - X, fy = v - Y;
    int tx = X / TT_TILE, xo = X - tx * TT_TILE;
    if (tx < 0 || tx >= s->tiles_x || !row[tx]) return 0;
    const unsigned short *d = row[tx];
    int x1 = xo + 1, t1 = tx;                        // the right neighbour may sit in the next tile
    if (x1 >= TT_TILE) { x1 = 0; t1 = tx + 1; if (t1 >= s->tiles_x || !row[t1]) { x1 = xo; t1 = tx; } }
    const unsigned short *d1 = row[t1];
    int y1 = mini(Y + 1, TT_TILE - 1);
    float a = d[Y * TT_TILE + xo], b = d1[Y * TT_TILE + x1];
    float c = d[y1 * TT_TILE + xo], e = d1[y1 * TT_TILE + x1];
    *out = lerpf(lerpf(a, b, fx), lerpf(c, e, fx), fy) / 65535.f;
    return 1;
}

int TerrainTiles::bake(const char *dir, float *hm, unsigned char *lm, int w, int h,
                       float lon0, float lat1, float cells_per_deg,
                       const unsigned char *protect, int *flips) {
    tt_set_t s; memset(&s, 0, sizeof s);
    if (!tt_set_load(&s, dir, "height")) return 0;
    // Mask flips are an experiment (MARE_TERRAIN_ISLANDS=1): the supplied 32768 map is a
    // *loose* Earth - recognisable continents, regionally wrong coasts (its Adriatic is half
    // filled in) - so taking its coastline wholesale buries exactly the island seas that
    // matter. The real islands are authored as vector data in data/geo_coast.h instead; the
    // map contributes what it is good at, relief and bathymetry.
    int allow_flips = 0;
    { const char *e = getenv("MARE_TERRAIN_ISLANDS"); if (e && atoi(e)) allow_flips = 1; }
    double t0 = diag_now();
    unsigned short *row[64] = { 0 };
    int changed = 0, newland = 0, missing = 0;
    size_t tile_bytes = (size_t)TT_TILE * TT_TILE * 2;
    // supersample offsets: the cell centre and a ring at a third of a cell, so one stray source
    // pixel cannot flip a 12 km cell and a real island (solid land across the cell) always does
    static const float off[9][2] = { {0,0}, {-.33f,-.33f}, {0,-.33f}, {.33f,-.33f},
                                     {-.33f,0}, {.33f,0}, {-.33f,.33f}, {0,.33f}, {.33f,.33f} };
    for (int ty = 0; ty < s.tiles_y && ty < 64; ++ty) {
        for (int tx = 0; tx < s.tiles_x && tx < 64; ++tx) {       // one band of tiles resident at a time
            char path[384]; snprintf(path, sizeof path, "%s/height_%d_%d.bin", s.dir, tx, ty);
            FILE *f = fopen(path, "rb");
            row[tx] = NULL;
            if (!f) { missing++; continue; }
            unsigned short *d = (unsigned short *)malloc(tile_bytes);
            if (d && fread(d, 1, tile_bytes, f) == tile_bytes) row[tx] = d; else free(d);
            fclose(f);
        }
        for (int y = 0; y < h; ++y) {
            float lat = lat1 - (y + 0.5f) / cells_per_deg;
            float vc = (s.lat1 - lat) / (s.lat1 - s.lat0) * (s.height - 1);
            if ((int)vc / TT_TILE != ty) continue;                 // this base row belongs to another band
            for (int x = 0; x < w; ++x) {
                float lon = lon0 + (x + 0.5f) / cells_per_deg;
                if (lon < s.lon0 || lon > s.lon1 || lat < s.lat0 || lat > s.lat1) continue;
                int i = y * w + x, votes = 0, got = 0;
                float hc = 0;
                for (int k = 0; k < 9; ++k) {
                    float u = (lon + off[k][0] / cells_per_deg - s.lon0) / (s.lon1 - s.lon0) * (s.width - 1);
                    float v = (s.lat1 - (lat + off[k][1] / cells_per_deg)) / (s.lat1 - s.lat0) * (s.height - 1);
                    float g;
                    if (!tt_band_sample(row, &s, ty, u, v, &g)) continue;
                    float hh = tt_h_of(g);
                    votes += hh > 0.03f; got++;
                    if (k == 0) hc = hh;
                }
                if (got < 9) continue;                             // tile missing under part of the cell
                int was_land = lm[i] != 0, keep = protect && protect[i];
                if (allow_flips && !was_land && votes >= 6 && !keep) {   // authored island (experiment, see below)
                    lm[i] = 1; hm[i] = maxf(hc, 0.45f); newland++; changed++;
                } else if (was_land && hc > 0.f && votes >= 3) {   // shared land: authored relief, where it is higher
                    // The source compresses its land into a narrow band, so taken as-is it
                    // flattened every coast into a plain. It adds the ranges it knows about;
                    // the procedural ridges stay where it is smooth.
                    if (hc > hm[i]) { hm[i] = hc; changed++; }
                } else if (!was_land && hc < 0.f && votes <= 1) {  // shared sea: authored bathymetry
                    hm[i] = minf(hc, -0.12f); changed++;
                }                                                  // shoreline disagreement: the base stands
            }
        }
        for (int tx = 0; tx < s.tiles_x && tx < 64; ++tx) { free(row[tx]); row[tx] = NULL; }
    }
    free(s.tile);
    if (flips) *flips = newland;
    LOG_INFO("terrain", "geography baked from %s: %d cells written (%d new land), %d tiles missing, %.0f ms",
             s.dir, changed, newland, missing, (diag_now() - t0) * 1000);
    return changed;
}

// --- the fine set, for the GPU height atlas (gfx/heightatlas.cc)

int TerrainTiles::fine_info(tt_setinfo_t *out) {
    const tt_set_t *s = &sets[TT_HEIGHT_FINE];
    memset(out, 0, sizeof *out);
    if (!s->present) return 0;
    out->present = 1; out->tiles_x = s->tiles_x; out->tiles_y = s->tiles_y;
    out->width = s->width; out->height = s->height; out->bytes_per_sample = s->bytes_per_sample;
    out->lon0 = s->lon0; out->lat0 = s->lat0; out->lon1 = s->lon1; out->lat1 = s->lat1;
    return 1;
}
const void *TerrainTiles::fine_tile(int tx, int ty) {
    const tt_set_t *s = &sets[TT_HEIGHT_FINE];
    if (!s->present || (unsigned)tx >= (unsigned)s->tiles_x || (unsigned)ty >= (unsigned)s->tiles_y) return NULL;
    const tt_tile_t *t = &s->tile[ty * s->tiles_x + tx];
    return t->state == TT_READY ? t->data : NULL;
}
int TerrainTiles::height_fine(float lon, float lat, float *out) {
    float g;
    if (!tt_sample_u16(&sets[TT_HEIGHT_FINE], lon, lat, &g)) return 0;
    *out = tt_h_of(g);
    return 1;
}

int TerrainTiles::resident(float lon, float lat) {
    int tx, ty; float fx, fy;
    const tt_set_t *s = &sets[TT_HEIGHT];
    if (!tt_index(s, lon, lat, &tx, &ty, &fx, &fy)) return 0;
    return s->tile[ty * s->tiles_x + tx].state == TT_READY;
}

void TerrainTiles::stats(tt_stats_t *out) {
    memset(out, 0, sizeof *out);
    for (int k = 0; k < TT_KINDS; ++k) {
        const tt_set_t *s = &sets[k]; if (!s->present) continue;
        for (int i = 0; i < s->ntiles; ++i) {
            out->resident += s->tile[i].state == TT_READY;
            out->loading  += s->tile[i].state == TT_QUEUED;
            out->wanted   += s->tile[i].last_want == tt_update_no;
        }
    }
    out->loaded_total = tt_loaded_total; out->evicted = tt_evicted;
    out->bytes = tt_bytes; out->budget_bytes = tt_budget;
    out->last_read_ms = tt_last_read_ms;
}

} // namespace mare::game::world

// C ABI (terraintiles.h) for the unported callers
extern "C" {
int      terraintiles_init(const char *dir) { return mare::game::world::terraintiles().init(dir); }
void     terraintiles_shutdown(void) { mare::game::world::terraintiles().shutdown(); }
void     terraintiles_update(float lon, float lat, float radius_deg) { mare::game::world::terraintiles().update(lon, lat, radius_deg); }
int      terraintiles_height(float lon, float lat, float *out) { return mare::game::world::terraintiles().height(lon, lat, out); }
int      terraintiles_normal(float lon, float lat, float out[3]) { return mare::game::world::terraintiles().normal(lon, lat, out); }
int      terraintiles_resident(float lon, float lat) { return mare::game::world::terraintiles().resident(lon, lat); }
void     terraintiles_stats(tt_stats_t *out) { mare::game::world::terraintiles().stats(out); }
unsigned terraintiles_generation(void) { return mare::game::world::terraintiles().generation(); }
int  terraintiles_fine_info(tt_setinfo_t *out) { return mare::game::world::terraintiles().fine_info(out); }
const void *terraintiles_fine_tile(int tx, int ty) { return mare::game::world::terraintiles().fine_tile(tx, ty); }
int  terraintiles_height_fine(float lon, float lat, float *out) { return mare::game::world::terraintiles().height_fine(lon, lat, out); }
float terraintiles_decode_u16(unsigned short v) { return mare::game::world::TerrainTiles::decode_u16(v); }
int terraintiles_bake(const char *dir, float *hm, unsigned char *lm, int w, int h,
                      float lon0, float lat1, float cells_per_deg,
                      const unsigned char *protect, int *flips) {
    return mare::game::world::terraintiles().bake(dir, hm, lm, w, h, lon0, lat1, cells_per_deg, protect, flips);
}
}
