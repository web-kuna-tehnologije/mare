// Streamed terrain tiles (see terraintiles.h). Sections: the tile index · the load pipeline on
// the job system · the wanted set and the budget · sampling.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "terraintiles.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../assets/asset.h"

enum { TT_HEIGHT = 0, TT_NORMAL, TT_KINDS };
static tt_set_t  sets[TT_KINDS];
static size_t    tt_bytes, tt_budget = (size_t)TT_BUDGET_MB * 1024 * 1024;
static unsigned  tt_update_no, tt_gen = 1;
static int       tt_loaded_total, tt_evicted, tt_inited;
static double    tt_last_read_ms;

// Sea level in the 16-bit heightfield, and the relief either side of it.
//
// Measured, not guessed - guessing floods continents. Sampling the cut tiles at known places
// (tools/terrain_calibrate.py --probe) gives land 0.54 (Andes) .. 0.63 (Amazon, Alps) and sea
// 0.22 (Atlantic abyss) .. 0.43 (shallow North Sea), so the shoreline sits at about 0.50.
//
// Note what those numbers say about the source: the Amazon lowland (0.612) and the Alps
// (0.624) are a hundredth apart, while the oceans use half the range. This map spends its
// dynamic range on bathymetry and compresses the land, so the authored *heights* are coarse
// however they are scaled - the land scale below is stretched to pull some relief out of that
// narrow band. The fine detail on land comes from the normal map, which is where a 32768
// square source actually pays.
#define TT_SEA           0.500f
#define TT_HEIGHT_SCALE  20.0f
#define TT_DEPTH_SCALE    9.0f

// ---------------------------------------------------------------------------
// index

static int tt_json_num(const char *s, const char *key, double *out) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    *out = atof(p + 1); return 1;
}

static int tt_set_load(tt_set_t *s, const char *dir, const char *kind) {
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

int terraintiles_init(const char *dir) {
    char fallback[320];
    // Opt-in. The tiling, the paging and the budget all work, but the supplied source map
    // compresses land into a tenth of its range (see the calibration note above), so the
    // relief it yields is not yet an improvement on the procedural world. MARE_TERRAIN_TILES=1
    // turns it on for tuning; see game/docs/DESIGN_TERRAIN.md.
    { const char *on = getenv("MARE_TERRAIN_TILES");
      if (!on || atoi(on) == 0) { LOG_INFO("terrain", "streamed terrain tiles off (MARE_TERRAIN_TILES=1 enables them)"); return 0; } }
    if (!dir) { snprintf(fallback, sizeof fallback, "%s/../terrain/tiles", asset_root()); dir = fallback; }
    terraintiles_shutdown();
    const char *e = getenv("MARE_TERRAIN_MB");
    if (e && atoi(e) > 0) tt_budget = (size_t)atoi(e) * 1024 * 1024;
    int ok = tt_set_load(&sets[TT_HEIGHT], dir, "height");
    tt_set_load(&sets[TT_NORMAL], dir, "normal");
    tt_inited = ok;
    if (!ok) LOG_INFO("terrain", "no streamed terrain under %s: the procedural world stands", dir);
    else     LOG_INFO("terrain", "streamed terrain ready, budget %zu MB", tt_budget / (1024 * 1024));
    return ok;
}

void terraintiles_shutdown(void) {
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

typedef struct tt_job_t { tt_set_t *s; tt_tile_t *t; void *data; size_t bytes; double t0, ms; } tt_job_t;

static void tt_publish(void *p) {                 // MAIN
    tt_job_t *j = (tt_job_t *)p;
    if (j->data) { j->t->data = j->data; j->t->state = TT_READY; tt_bytes += j->bytes;
                   tt_loaded_total++; tt_last_read_ms = j->ms; tt_gen++; }
    else         { j->t->state = TT_EMPTY; }
    free(j);
}

static void tt_read(void *p) {                    // IO
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
    jobs_submit(JOB_LANE_MAIN, tt_publish, j, 0);
}

static void tt_request(tt_set_t *s, tt_tile_t *t) {
    if (t->state != TT_EMPTY) return;
    tt_job_t *j = (tt_job_t *)calloc(1, sizeof *j);
    j->s = s; j->t = t; j->bytes = (size_t)TT_TILE * TT_TILE * s->bytes_per_sample;
    t->state = TT_QUEUED;
    jobs_submit(JOB_LANE_IO, tt_read, j, 0);
}

// ---------------------------------------------------------------------------
// wanted set and budget

static int tt_index(const tt_set_t *s, float lon, float lat, int *tx, int *ty, float *fx, float *fy) {
    if (!s->present || lon < s->lon0 || lon > s->lon1 || lat < s->lat0 || lat > s->lat1) return 0;
    float u = (lon - s->lon0) / (s->lon1 - s->lon0) * (s->width  - 1);
    float v = (s->lat1 - lat) / (s->lat1 - s->lat0) * (s->height - 1);
    int X = (int)u, Y = (int)v;
    *tx = X / TT_TILE; *ty = Y / TT_TILE;
    if (*tx < 0 || *ty < 0 || *tx >= s->tiles_x || *ty >= s->tiles_y) return 0;
    *fx = u - *tx * TT_TILE; *fy = v - *ty * TT_TILE;
    return 1;
}

void terraintiles_update(float lon, float lat, float radius_deg) {
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

static int tt_sample_u16(const tt_set_t *s, float lon, float lat, float *out) {
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

int terraintiles_height(float lon, float lat, float *out) {
    float g;
    if (!tt_sample_u16(&sets[TT_HEIGHT], lon, lat, &g)) return 0;
    *out = g >= TT_SEA ? (g - TT_SEA) / (1.f - TT_SEA) * TT_HEIGHT_SCALE
                       : -(TT_SEA - g) / TT_SEA * TT_DEPTH_SCALE;
    return 1;
}

int terraintiles_normal(float lon, float lat, float out[3]) {
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

int terraintiles_resident(float lon, float lat) {
    int tx, ty; float fx, fy;
    const tt_set_t *s = &sets[TT_HEIGHT];
    if (!tt_index(s, lon, lat, &tx, &ty, &fx, &fy)) return 0;
    return s->tile[ty * s->tiles_x + tx].state == TT_READY;
}

unsigned terraintiles_generation(void) { return tt_gen; }

void terraintiles_stats(tt_stats_t *out) {
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
