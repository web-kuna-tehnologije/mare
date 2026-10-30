// The GEBCO export read as chunks (see geochunks.h and assets/data/how_to.txt).
// Sections: the manifest and the chunk grid · the load pipeline on the job system · the
// wanted set and the budget · sampling.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "geochunks.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../assets/asset.h"
}

namespace mare::game::world {

// The calibration the whole game is tuned against (world/terraintiles.cc TT_*): the u16 cut
// encoded land linearly to 9000 m and the sea sqrt-compressed to 12000 m, and the renderer
// decoded that to these world ranges. Reading the float32 source directly has to land on the
// same numbers or every shallow-water gate, wave fade and biome threshold shifts under it.
// The sqrt on the sea half is deliberate and explained in tools/terrain_from_gebco.py: a
// linear real-metre shelf puts the whole north Adriatic at ~0.04 units and washes every
// coastal sea into the shallow colour.
#define GC_LAND_MAX_M   9000.f
#define GC_DEPTH_MAX_M 12000.f
#define GC_HEIGHT_SCALE   20.f
#define GC_DEPTH_SCALE     9.f

typedef struct gc_chunk_t {
    unsigned char state;              // 0 empty, 1 queued, 2 ready
    short         cx, cy;
    float        *height;             // GC_SAMPLES^2 metres when ready
    unsigned char *coverage;          // GC_SAMPLES^2, 1 = valid sample
    unsigned      last_want;
    unsigned char absent;             // the files are not there: never ask again
} gc_chunk_t;

typedef struct gc_job_t { gc_chunk_t *c; float *height; unsigned char *coverage; double ms; } gc_job_t;

#define GC_BAKE_SLOTS 64        // worker slots the bake's per-slot counters cover
class GeoChunks;
// one chunk row's worth of work for the geography fold, shared by the workers folding it
typedef struct bake_band_t {
    GeoChunks *self;
    float **row; unsigned char **cov;
    float *hm; unsigned char *lm; const unsigned char *protect;
    int w, h, cy, y0;
    float lon0, lat1, cells_per_deg, gw, gh;
    int allow_flips;
    int tally[GC_BAKE_SLOTS][2];   // [0] cells written, [1] new land
} bake_band_t;

enum { GC_EMPTY = 0, GC_QUEUED, GC_READY };

class GeoChunks {
public:
    int      init(const char *dir);
    void     shutdown();
    void     update(float lon, float lat, float radius_deg);
    int      height_m(float lon, float lat, float *out);
    int      height_world(float lon, float lat, float *out);
    int      resident(float lon, float lat);
    int      bake(float *hm, unsigned char *lm, int w, int h, float lon0, float lat1,
                  float cells_per_deg, const unsigned char *protect, int *flips);
    static void bake_rows(void *arg, int begin, int end, int slot);   // one band, on the pool

    friend struct bake_band_t_access;
    void     stats(gc_stats_t *out);
    unsigned generation() { return gen; }

public:
    // read by bake_rows on the worker threads (the fold is the only thing that touches them
    // from anywhere but the main thread, and only for reading)
    char  root[320] = {};
    int   present = 0, inited = 0;
    int   chunks_x = 0, chunks_y = 0;
    float bbox_w = -180.f, bbox_s = -90.f, bbox_e = 180.f, bbox_n = 90.f;
private:
    gc_chunk_t *chunk = nullptr; int nchunks = 0;
    size_t bytes = 0, budget = (size_t)GC_BUDGET_MB * 1024 * 1024;
    unsigned update_no = 0, gen = 1;
    int loaded_total = 0, evicted = 0, missing_files = 0;
    double last_read_ms = 0;

    static size_t chunk_bytes() { return (size_t)GC_SAMPLES * GC_SAMPLES * (sizeof(float) + 1); }
    static int  json_num(const char *s, const char *key, double *out);
    static int  json_pair(const char *s, const char *key, double *a, double *b);
    static int  json_array4(const char *s, const char *key, double *v4);
    static char *read_text(const char *path, size_t *n);
    const char *probe_dir(char *buf, size_t n);
    int  parse_manifest();
    void verify_bounds();
    gc_chunk_t *at(int cx, int cy) { return (unsigned)cx < (unsigned)chunks_x && (unsigned)cy < (unsigned)chunks_y ? &chunk[cy * chunks_x + cx] : nullptr; }
    int  locate(float lon, float lat, int *cx, int *cy, float *fx, float *fy);
    void request(gc_chunk_t *c);
    void publish(void *p);
    static void publish_cb(void *p);
    static void read_cb(void *p);
};

GeoChunks &geochunks() { static GeoChunks g; return g; }

// ---------------------------------------------------------------------------
// the manifest and the chunk grid

char *GeoChunks::read_text(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > (1 << 20)) { fclose(f); return nullptr; }
    char *s = (char *)malloc((size_t)len + 1);
    size_t got = s ? fread(s, 1, (size_t)len, f) : 0;
    fclose(f);
    if (!s) return nullptr;
    s[got] = 0; if (n) *n = got;
    return s;
}

int GeoChunks::json_num(const char *s, const char *key, double *out) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    *out = atof(p + 1); return 1;
}
// "key": [ a, b ] - the manifest writes its arrays over several lines
int GeoChunks::json_pair(const char *s, const char *key, double *a, double *b) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, '['); if (!p) return 0;
    char *end = nullptr;
    *a = strtod(p + 1, &end); if (!end) return 0;
    const char *comma = strchr(end, ','); if (!comma) return 0;
    *b = strtod(comma + 1, nullptr);
    return 1;
}
int GeoChunks::json_array4(const char *s, const char *key, double *v4) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, '['); if (!p) return 0;
    const char *q = p + 1; char *end = nullptr;
    for (int i = 0; i < 4; ++i) {
        v4[i] = strtod(q, &end);
        if (end == q) return 0;
        q = strchr(end, ','); if (!q && i < 3) return 0;
        if (q) q++;
    }
    return 1;
}

// Where the export lives. The asset root is the per-worktree tree (assets/elm), and the raw
// export sits beside it under assets/data, so the first probe is the sibling; the rest let
// the game find it from a build directory or a staged copy, as asset_root() does.
const char *GeoChunks::probe_dir(char *buf, size_t n) {
    const char *env = getenv("MARE_GEOCHUNK_DIR");
    if (env) { snprintf(buf, n, "%s", env); return buf; }
    static const char *cands[] = {
        "%s/../data/export-global", "%s/data/export-global",
    };
    for (unsigned i = 0; i < sizeof cands / sizeof *cands; ++i) {
        snprintf(buf, n, cands[i], asset_root());
        char probe[384]; snprintf(probe, sizeof probe, "%s/manifest.json", buf);
        FILE *f = fopen(probe, "rb"); if (f) { fclose(f); return buf; }
    }
    static const char *plain[] = { "assets/data/export-global", "../assets/data/export-global",
                                   "../../assets/data/export-global", "../../../assets/data/export-global" };
    for (unsigned i = 0; i < sizeof plain / sizeof *plain; ++i) {
        snprintf(buf, n, "%s", plain[i]);
        char probe[384]; snprintf(probe, sizeof probe, "%s/manifest.json", buf);
        FILE *f = fopen(probe, "rb"); if (f) { fclose(f); return buf; }
    }
    snprintf(buf, n, "%s/../data/export-global", asset_root());
    return buf;
}

int GeoChunks::parse_manifest() {
    char path[384]; snprintf(path, sizeof path, "%s/manifest.json", root);
    size_t n = 0; char *js = read_text(path, &n);
    if (!js) { LOG_DEBUG("terrain", "no %s", path); return 0; }
    double a = 0, b = 0, v4[4] = { -180, -90, 180, 90 }, samples = GC_SAMPLES, cells = GC_CELLS;
    int ok = json_pair(js, "chunk_count", &a, &b);
    json_array4(js, "bbox", v4);
    json_num(js, "samples_per_chunk", &samples);
    json_num(js, "cells_per_chunk", &cells);
    free(js);
    if (!ok) { LOG_WARN("terrain", "%s: no chunk_count", path); return 0; }
    chunks_x = (int)a; chunks_y = (int)b;
    bbox_w = (float)v4[0]; bbox_s = (float)v4[1]; bbox_e = (float)v4[2]; bbox_n = (float)v4[3];
    if (chunks_x <= 0 || chunks_y <= 0 || chunks_x * chunks_y > GC_MAX_CHUNKS) {
        LOG_WARN("terrain", "%s: %dx%d chunks is out of range", path, chunks_x, chunks_y); return 0; }
    if ((int)samples != GC_SAMPLES || (int)cells != GC_CELLS) {
        LOG_WARN("terrain", "%s: %d samples / %d cells per chunk, this build reads %d/%d",
                 path, (int)samples, (int)cells, GC_SAMPLES, GC_CELLS); return 0; }
    return 1;
}

// The how_to derives a chunk's bounds from the manifest (bbox split chunk_count ways), and
// every chunk also ships its own tile.json. Read one and check the two agree, so a future
// export with a different layout is caught here instead of as crooked terrain.
void GeoChunks::verify_bounds() {
    char path[384]; snprintf(path, sizeof path, "%s/chunks/0/0/tile.json", root);
    size_t n = 0; char *js = read_text(path, &n);
    if (!js) return;
    double v4[4];
    int ok = json_array4(js, "bounds_wgs84", v4);
    free(js);
    if (!ok) return;
    float w = bbox_w, nlat = bbox_n;
    float e = bbox_w + (bbox_e - bbox_w) / chunks_x, s = bbox_n - (bbox_n - bbox_s) / chunks_y;
    float d = fabsf((float)v4[0] - w) + fabsf((float)v4[1] - s) + fabsf((float)v4[2] - e) + fabsf((float)v4[3] - nlat);
    if (d > 1e-3f)
        LOG_WARN("terrain", "chunk 0,0 says %.3f,%.3f..%.3f,%.3f but the manifest grid gives %.3f,%.3f..%.3f,%.3f",
                 v4[0], v4[1], v4[2], v4[3], w, s, e, nlat);
    else
        LOG_DEBUG("terrain", "chunk bounds verified against chunks/0/0/tile.json");
}

int GeoChunks::init(const char *dir) {
    shutdown();
    { const char *on = getenv("MARE_GEOCHUNKS");
      if (on && atoi(on) == 0) { LOG_INFO("terrain", "geo chunks off (MARE_GEOCHUNKS=0)"); return 0; } }
    char buf[320];
    if (dir) snprintf(buf, sizeof buf, "%s", dir); else probe_dir(buf, sizeof buf);
    snprintf(root, sizeof root, "%s", buf);
    if (!parse_manifest()) { LOG_INFO("terrain", "no GEBCO chunk export under %s: the cut tile set stands", root); return 0; }
    { const char *e = getenv("MARE_GEOCHUNK_MB"); if (e && atoi(e) > 0) budget = (size_t)atoi(e) * 1024 * 1024; }
    nchunks = chunks_x * chunks_y;
    chunk = (gc_chunk_t *)calloc((size_t)nchunks, sizeof *chunk);
    for (int y = 0; y < chunks_y; ++y) for (int x = 0; x < chunks_x; ++x) {
        gc_chunk_t *c = &chunk[y * chunks_x + x]; c->cx = (short)x; c->cy = (short)y;
    }
    verify_bounds();
    present = inited = 1;
    LOG_INFO("terrain", "GEBCO chunks: %dx%d of %d samples (float32 metres + coverage), lon %.0f..%.0f lat %.0f..%.0f, budget %zu MB, %s",
             chunks_x, chunks_y, GC_SAMPLES, bbox_w, bbox_e, bbox_s, bbox_n, budget / (1024 * 1024), root);
    return 1;
}

void GeoChunks::shutdown() {
    if (chunk) {
        for (;;) { int busy = 0; for (int i = 0; i < nchunks; ++i) busy += chunk[i].state == GC_QUEUED;
                   if (!busy) break; jobs_help(); }
        for (int i = 0; i < nchunks; ++i) { free(chunk[i].height); free(chunk[i].coverage); }
        free(chunk);
    }
    chunk = nullptr; nchunks = 0; bytes = 0; present = inited = 0;
}

// ---------------------------------------------------------------------------
// load pipeline: IO read -> MAIN publish

void GeoChunks::publish_cb(void *p) { geochunks().publish(p); }

void GeoChunks::publish(void *p) {           // MAIN
    gc_job_t *j = (gc_job_t *)p;
    if (j->height && j->coverage) {
        j->c->height = j->height; j->c->coverage = j->coverage; j->c->state = GC_READY;
        bytes += chunk_bytes(); loaded_total++; last_read_ms = j->ms; gen++;
    } else {
        free(j->height); free(j->coverage);
        j->c->state = GC_EMPTY; j->c->absent = 1; missing_files++;
    }
    free(j);
}

void GeoChunks::read_cb(void *p) {           // IO
    gc_job_t *j = (gc_job_t *)p;
    GeoChunks &g = geochunks();
    char path[448];
    double t0 = diag_now();
    size_t nsamp = (size_t)GC_SAMPLES * GC_SAMPLES;
    // "chunks/{y}/{x}" (manifest chunk_directory); y = 0 is the northern row, x = 0 the west
    snprintf(path, sizeof path, "%s/chunks/%d/%d/height.r32", g.root, j->c->cy, j->c->cx);
    FILE *f = fopen(path, "rb");
    if (f) {
        float *h = (float *)malloc(nsamp * sizeof(float));
        // the how_to is explicit: check that exactly 513 x 513 elements were read
        if (h && fread(h, sizeof(float), nsamp, f) == nsamp) j->height = h; else free(h);
        fclose(f);
    }
    if (j->height) {
        snprintf(path, sizeof path, "%s/chunks/%d/%d/coverage.r8", g.root, j->c->cy, j->c->cx);
        f = fopen(path, "rb");
        if (f) {
            unsigned char *c = (unsigned char *)malloc(nsamp);
            if (c && fread(c, 1, nsamp, f) == nsamp) j->coverage = c; else free(c);
            fclose(f);
        }
        // a chunk with heights but no coverage file is taken as fully covered: the mask is
        // there to find holes, and its absence is not a reason to drop real samples
        if (!j->coverage) { j->coverage = (unsigned char *)malloc(nsamp); if (j->coverage) memset(j->coverage, 1, nsamp); }
    }
    j->ms = (diag_now() - t0) * 1000;
    jobs_submit(JOB_LANE_MAIN, publish_cb, j, 0);
}

void GeoChunks::request(gc_chunk_t *c) {
    if (c->state != GC_EMPTY || c->absent) return;
    gc_job_t *j = (gc_job_t *)calloc(1, sizeof *j);
    j->c = c;
    c->state = GC_QUEUED;
    jobs_submit(JOB_LANE_IO, read_cb, j, 0);
}

// ---------------------------------------------------------------------------
// the wanted set and the budget

// lon/lat -> chunk and the fractional sample position inside it. The global sample grid is
// chunks_x * GC_CELLS + 1 wide: neighbouring chunks share their edge samples, which is why
// the export's joins agree to 0 m and why fx can reach exactly GC_CELLS.
int GeoChunks::locate(float lon, float lat, int *cx, int *cy, float *fx, float *fy) {
    if (!present || lon < bbox_w || lon > bbox_e || lat < bbox_s || lat > bbox_n) return 0;
    float u = (lon - bbox_w) / (bbox_e - bbox_w) * (float)(chunks_x * GC_CELLS);
    float v = (bbox_n - lat) / (bbox_n - bbox_s) * (float)(chunks_y * GC_CELLS);
    int X = (int)u, Y = (int)v;
    *cx = X / GC_CELLS; *cy = Y / GC_CELLS;
    if (*cx >= chunks_x) { *cx = chunks_x - 1; }
    if (*cy >= chunks_y) { *cy = chunks_y - 1; }
    *fx = u - (float)(*cx * GC_CELLS);
    *fy = v - (float)(*cy * GC_CELLS);
    return 1;
}

void GeoChunks::update(float lon, float lat, float radius_deg) {
    if (!inited) return;
    update_no++;
    int started = 0;
    int x0, y0, x1, y1; float fx, fy;
    if (!locate(clampf(lon - radius_deg, bbox_w, bbox_e), clampf(lat + radius_deg, bbox_s, bbox_n), &x0, &y0, &fx, &fy)) return;
    if (!locate(clampf(lon + radius_deg, bbox_w, bbox_e), clampf(lat - radius_deg, bbox_s, bbox_n), &x1, &y1, &fx, &fy)) return;
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        gc_chunk_t *c = at(x, y); if (!c) continue;
        c->last_want = update_no;
        if (c->state == GC_EMPTY && !c->absent && started < GC_MAX_INFLIGHT) { request(c); started++; }
    }
    for (int guard = 0; guard < 8 && bytes > budget; ++guard) {
        gc_chunk_t *victim = nullptr;
        for (int i = 0; i < nchunks; ++i) {
            gc_chunk_t *c = &chunk[i];
            if (c->state != GC_READY || c->last_want == update_no) continue;
            if (!victim || c->last_want < victim->last_want) victim = c;
        }
        if (!victim) break;
        free(victim->height); free(victim->coverage);
        victim->height = nullptr; victim->coverage = nullptr; victim->state = GC_EMPTY;
        bytes -= chunk_bytes(); evicted++; gen++;
    }
}

// ---------------------------------------------------------------------------
// sampling

int GeoChunks::height_m(float lon, float lat, float *out) {
    int cx, cy; float fx, fy;
    if (!locate(lon, lat, &cx, &cy, &fx, &fy)) return 0;
    const gc_chunk_t *c = at(cx, cy);
    if (!c || c->state != GC_READY) return 0;
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 > GC_CELLS - 1) x0 = GC_CELLS - 1;
    if (y0 > GC_CELLS - 1) y0 = GC_CELLS - 1;
    int x1 = x0 + 1, y1 = y0 + 1;              // always inside: 513 samples, 512 cells
    float tx = fx - x0, ty = fy - y0;
    const unsigned char *cv = c->coverage;
    size_t i00 = (size_t)y0 * GC_SAMPLES + x0, i10 = (size_t)y0 * GC_SAMPLES + x1;
    size_t i01 = (size_t)y1 * GC_SAMPLES + x0, i11 = (size_t)y1 * GC_SAMPLES + x1;
    // coverage.r8: 0 means the export had no input sample there. The how_to is explicit that
    // this is not a land/sea mask - a hole is simply unknown ground, so the caller keeps what
    // it already had rather than being handed a fabricated height.
    if (!cv[i00] || !cv[i10] || !cv[i01] || !cv[i11]) return 0;
    const float *h = c->height;
    *out = lerpf(lerpf(h[i00], h[i10], tx), lerpf(h[i01], h[i11], tx), ty);
    return 1;
}

int GeoChunks::height_world(float lon, float lat, float *out) {
    float m;
    if (!height_m(lon, lat, &m)) return 0;
    *out = m >= 0.f ? minf(m / GC_LAND_MAX_M, 1.f) * GC_HEIGHT_SCALE
                    : -sqrtf(minf(-m / GC_DEPTH_MAX_M, 1.f)) * GC_DEPTH_SCALE;
    return 1;
}

// ---------------------------------------------------------------------------
// the one-shot geography fold (see geochunks.h)

// Bilinear sample from a loaded row of chunks. `row[cx]` holds that chunk's heights and
// `cov[cx]` its coverage; both are NULL where the chunk is missing. Returns 0 (and leaves
// `out` alone) outside the row, on a missing chunk, or where coverage says the sample is not
// real - the how_to is explicit that a hole is unknown ground, not sea.
static int gc_row_sample(float **row, unsigned char **cov, int chunks_x, int cyrow,
                         float u, float v, float *out) {
    int X = (int)u, Y = (int)v;
    int cx = X / GC_CELLS;
    if (cx < 0 || cx >= chunks_x || !row[cx]) return 0;
    if (Y / GC_CELLS != cyrow) return 0;
    int lx = X - cx * GC_CELLS, ly = Y - cyrow * GC_CELLS;
    if (lx > GC_CELLS - 1) lx = GC_CELLS - 1;
    if (ly > GC_CELLS - 1) ly = GC_CELLS - 1;
    float fx = u - (float)(cx * GC_CELLS + lx), fy = v - (float)(cyrow * GC_CELLS + ly);
    const float *h = row[cx]; const unsigned char *c = cov[cx];
    size_t i00 = (size_t)ly * GC_SAMPLES + lx, i10 = i00 + 1;
    size_t i01 = i00 + GC_SAMPLES, i11 = i01 + 1;
    if (!c[i00] || !c[i10] || !c[i01] || !c[i11]) return 0;
    *out = lerpf(lerpf(h[i00], h[i10], fx), lerpf(h[i01], h[i11], fx), fy);
    return 1;
}

// one band of base rows, folded on a worker; `tally` is per slot so nothing is contended
void GeoChunks::bake_rows(void *arg, int begin, int end, int slot) {
    bake_band_t *b = (bake_band_t *)arg;
    int k = (unsigned)slot < GC_BAKE_SLOTS ? slot : 0;
    static const float off[9][2] = { {0,0}, {-.33f,-.33f}, {0,-.33f}, {.33f,-.33f},
                                     {-.33f,0}, {.33f,0}, {-.33f,.33f}, {0,.33f}, {.33f,.33f} };
    const float bw = b->self->bbox_w, be = b->self->bbox_e, bs = b->self->bbox_s, bn = b->self->bbox_n;
    for (int r = begin; r < end; ++r) {
        int y = b->y0 + r;
        float lat = b->lat1 - (y + 0.5f) / b->cells_per_deg;
        for (int x = 0; x < b->w; ++x) {
            float lon = b->lon0 + (x + 0.5f) / b->cells_per_deg;
            if (lon < bw || lon > be || lat < bs || lat > bn) continue;
            int i = y * b->w + x, votes = 0, got = 0;
            float hc = 0;
            for (int q = 0; q < 9; ++q) {
                float u = (lon + off[q][0] / b->cells_per_deg - bw) / (be - bw) * b->gw;
                float v = (bn - (lat + off[q][1] / b->cells_per_deg)) / (bn - bs) * b->gh;
                float m;
                if (!gc_row_sample(b->row, b->cov, b->self->chunks_x, b->cy, u, v, &m)) continue;
                float hh = m >= 0.f ? minf(m / GC_LAND_MAX_M, 1.f) * GC_HEIGHT_SCALE
                                    : -sqrtf(minf(-m / GC_DEPTH_MAX_M, 1.f)) * GC_DEPTH_SCALE;
                votes += hh > 0.03f; got++;
                if (q == 0) hc = hh;
            }
            if (got < 9) continue;                           // a hole under part of the cell
            int was_land = b->lm[i] != 0, keep = b->protect && b->protect[i];
            if (b->allow_flips && !was_land && votes >= 6 && !keep) {
                b->lm[i] = 1; b->hm[i] = maxf(hc, 0.45f); b->tally[k][1]++; b->tally[k][0]++;
            } else if (was_land && hc > 0.f && votes >= 3) {
                if (hc > b->hm[i]) { b->hm[i] = hc; b->tally[k][0]++; }
            } else if (!was_land && hc < 0.f && votes <= 1) {
                b->hm[i] = minf(hc, -0.12f); b->tally[k][0]++;
            }
        }
    }
}

int GeoChunks::bake(float *hm, unsigned char *lm, int w, int h, float lon0, float lat1,
                    float cells_per_deg, const unsigned char *protect, int *flips) {
    if (!present) return 0;
    int allow_flips = 0;
    { const char *e = getenv("MARE_TERRAIN_ISLANDS"); if (e && atoi(e)) allow_flips = 1; }
    double t0 = diag_now();
    int changed = 0, newland = 0, missing = 0;
    size_t nsamp = (size_t)GC_SAMPLES * GC_SAMPLES;
    // (the cell centre plus a ring at a third of a cell - one stray source sample must not
    // flip a 7 km cell, and a real island always does - lives in bake_rows with the sampling)
    float **row = (float **)calloc((size_t)chunks_x, sizeof *row);
    unsigned char **cov = (unsigned char **)calloc((size_t)chunks_x, sizeof *cov);
    if (!row || !cov) { free(row); free(cov); return 0; }
    float gw = (float)(chunks_x * GC_CELLS), gh = (float)(chunks_y * GC_CELLS);
    for (int cy = 0; cy < chunks_y; ++cy) {
        // does this chunk row touch the base grid at all? skip the whole read if not
        float rlat1 = bbox_n - (float)cy * (bbox_n - bbox_s) / chunks_y;
        float rlat0 = bbox_n - (float)(cy + 1) * (bbox_n - bbox_s) / chunks_y;
        float blat1 = lat1, blat0 = lat1 - h / cells_per_deg;
        if (rlat0 > blat1 || rlat1 < blat0) continue;
        for (int cx = 0; cx < chunks_x; ++cx) {
            char path[448];
            snprintf(path, sizeof path, "%s/chunks/%d/%d/height.r32", root, cy, cx);
            FILE *f = fopen(path, "rb");
            if (!f) { missing++; continue; }
            float *d = (float *)malloc(nsamp * sizeof(float));
            if (d && fread(d, sizeof(float), nsamp, f) == nsamp) row[cx] = d; else free(d);
            fclose(f);
            if (!row[cx]) continue;
            snprintf(path, sizeof path, "%s/chunks/%d/%d/coverage.r8", root, cy, cx);
            f = fopen(path, "rb");
            if (f) { unsigned char *c = (unsigned char *)malloc(nsamp);
                     if (c && fread(c, 1, nsamp, f) == nsamp) cov[cx] = c; else free(c);
                     fclose(f); }
            if (!cov[cx]) { cov[cx] = (unsigned char *)malloc(nsamp); if (cov[cx]) memset(cov[cx], 1, nsamp); }
        }
        // Which base rows fall in this band, computed rather than scanned: the old loop
        // walked all 2080 rows once per chunk row. The rows are then folded in parallel -
        // each writes its own cells, so the only shared state is the counters, and those are
        // per worker slot.
        float lat_hi = bbox_n - (float)cy * (bbox_n - bbox_s) / chunks_y;
        float lat_lo = bbox_n - (float)(cy + 1) * (bbox_n - bbox_s) / chunks_y;
        int y0 = (int)ceilf((lat1 - lat_hi) * cells_per_deg - 0.5f);
        int y1 = (int)floorf((lat1 - lat_lo) * cells_per_deg - 0.5f);
        if (y0 < 0) y0 = 0; if (y1 > h - 1) y1 = h - 1;
        if (y1 >= y0) {
            bake_band_t bb = { this, row, cov, hm, lm, protect, w, h, cy, y0, lon0, lat1, cells_per_deg, gw, gh, allow_flips, {} };
            if (jobs_worker_count() > 0) jobs_parallel_for(JOB_LANE_WORKER, bake_rows, &bb, y1 - y0 + 1, 8);
            else bake_rows(&bb, 0, y1 - y0 + 1, 0);
            for (int k = 0; k < GC_BAKE_SLOTS; ++k) { changed += bb.tally[k][0]; newland += bb.tally[k][1]; }
        }
        for (int cx = 0; cx < chunks_x; ++cx) { free(row[cx]); row[cx] = NULL; free(cov[cx]); cov[cx] = NULL; }
    }
    free(row); free(cov);
    if (flips) *flips = newland;
    LOG_INFO("terrain", "geography baked from the GEBCO chunks: %d cells written (%d new land), %d chunks absent, %.0f ms",
             changed, newland, missing, (diag_now() - t0) * 1000);
    return changed;
}

int GeoChunks::resident(float lon, float lat) {
    int cx, cy; float fx, fy;
    if (!locate(lon, lat, &cx, &cy, &fx, &fy)) return 0;
    const gc_chunk_t *c = at(cx, cy);
    return c && c->state == GC_READY;
}

void GeoChunks::stats(gc_stats_t *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < nchunks; ++i) {
        out->resident += chunk[i].state == GC_READY;
        out->loading  += chunk[i].state == GC_QUEUED;
        out->wanted   += chunk[i].last_want == update_no;
    }
    out->loaded_total = loaded_total; out->evicted = evicted; out->missing_files = missing_files;
    out->bytes = bytes; out->budget_bytes = budget; out->last_read_ms = last_read_ms;
    out->present = present; out->chunks_x = chunks_x; out->chunks_y = chunks_y;
}

} // namespace mare::game::world

// C ABI (geochunks.h) for the unported callers
extern "C" {
int  geochunks_init(const char *dir) { return mare::game::world::geochunks().init(dir); }
void geochunks_shutdown(void) { mare::game::world::geochunks().shutdown(); }
void geochunks_update(float lon, float lat, float radius_deg) { mare::game::world::geochunks().update(lon, lat, radius_deg); }
int  geochunks_height_m(float lon, float lat, float *out) { return mare::game::world::geochunks().height_m(lon, lat, out); }
int  geochunks_height(float lon, float lat, float *out) { return mare::game::world::geochunks().height_world(lon, lat, out); }
int  geochunks_resident(float lon, float lat) { return mare::game::world::geochunks().resident(lon, lat); }
void geochunks_stats(gc_stats_t *out) { mare::game::world::geochunks().stats(out); }
unsigned geochunks_generation(void) { return mare::game::world::geochunks().generation(); }
int  geochunks_bake(float *hm, unsigned char *lm, int w, int h, float lon0, float lat1,
                    float cells_per_deg, const unsigned char *protect, int *flips) {
    return mare::game::world::geochunks().bake(hm, lm, w, h, lon0, lat1, cells_per_deg, protect, flips);
}
}
