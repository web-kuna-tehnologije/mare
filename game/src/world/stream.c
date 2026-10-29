// World streaming (see stream.h, game/docs/DESIGN_STREAMING.md). Sections: grids and tile
// storage · disk cache (read/validate/write) · deterministic delta generation · the load
// pipeline on the job system (IO read -> DECODE generate -> IO write-back -> MAIN commit) ·
// wanted set (quadtree descent) · memory budget + LRU eviction · lookups and stats.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stream.h"
#include "world.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#ifdef _WIN32
#include <direct.h>
#define stream_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define stream_mkdir(p) mkdir(p, 0755)
#endif

#define STREAM_MAGIC   0x4C49544Du   // 'MTIL'
#define STREAM_VERSION 1u
#define STREAM_PAYLOAD ((size_t)STREAM_RES * STREAM_RES * sizeof(float))

// detail bands per level: amplitude in world units and noise frequency in cycles per degree.
// L0/L1 are at or below the base map's resolution, so they carry no delta (zero payload): they
// exist as residency markers and coarse fallbacks, which keeps every code path uniform.
static const float lvl_amp[STREAM_LEVELS]  = { 0.f, 0.f, 0.30f, 0.50f };
static const float lvl_freq[STREAM_LEVELS] = { 0.f, 0.f, 3.0f,  6.0f  };

static int   grid_w[STREAM_LEVELS], grid_h[STREAM_LEVELS], lvl_off[STREAM_LEVELS + 1];
static stream_tile_t *tiles;                 // all levels, level-major
static int   ntiles;
static unsigned stream_seed, residency_gen = 1, update_no;
static char  tile_dir[256];
static size_t resident_bytes, budget_bytes = 64u * 1024u * 1024u;
static int   st_evicted, st_disk_hits, st_generated, st_water;
static double st_gen_ms, st_io_ms;
static int   initialized;

static int lvl_cells(int level) { return STREAM_L0_CELLS >> level; }

static stream_tile_t *tile_get(int level, int tx, int ty) {
    if (level < 0 || level >= STREAM_LEVELS || tx < 0 || ty < 0 || tx >= grid_w[level] || ty >= grid_h[level]) return 0;
    return &tiles[lvl_off[level] + ty * grid_w[level] + tx];
}
stream_tile_t *stream_tile(int level, int tx, int ty) { return tile_get(level, tx, ty); }
stream_tile_t *stream_tile_at_index(int i) { return i >= 0 && i < ntiles ? &tiles[i] : 0; }
int stream_tile_count(void) { return ntiles; }
int stream_grid_w(int level) { return level >= 0 && level < STREAM_LEVELS ? grid_w[level] : 0; }
int stream_grid_h(int level) { return level >= 0 && level < STREAM_LEVELS ? grid_h[level] : 0; }

// nominal extent in degrees (edge tiles reach past the world; samples there clamp like the base map)
static void tile_geo(const stream_tile_t *t, float *lon0, float *lat1, float *deg) {
    *deg  = lvl_cells(t->level) / (float)CELLS_PER_DEG;
    *lon0 = LON0 + t->tx * *deg;
    *lat1 = LAT1 - t->ty * *deg;                       // tiles count from the north-west corner
}
void stream_tile_rect(const stream_tile_t *t, float *x0, float *z0, float *x1, float *z1) {
    float lon0, lat1, deg; tile_geo(t, &lon0, &lat1, &deg);
    *x0 = lon2x(lon0); *x1 = lon2x(minf(lon0 + deg, LON1));
    *z0 = lat2z(lat1); *z1 = lat2z(maxf(lat1 - deg, LAT0));
}

static const char *l1_names[5][6] = {
    { "Hudson Bay & Shield", "Great Lakes & Labrador", "Newfoundland & N Atlantic", "British Isles & Norway", "Baltic & Central Europe", "Muscovy & Black Sea N" },
    { "New Spain Pacific", "Gulf of Mexico & Florida", "Caribbean & Sargasso", "Azores & Canaries", "Mediterranean & Sahara", "Levant & Egypt" },
    { "Eastern Pacific", "Peru & New Granada", "Brazil & Amazon", "Mid-Atlantic", "Guinea & Congo coast", "East African rim" },
    { "South Pacific", "Chile & W Patagonia", "La Plata & E Patagonia", "South Atlantic", "Cape of Good Hope", "SW Indian Ocean" },
    { "Southern Ocean", "Southern Ocean", "Southern Ocean", "Southern Ocean", "Southern Ocean", "Southern Ocean" },
};
const char *stream_l1_name(int tx, int ty) { return (unsigned)tx < 6 && (unsigned)ty < 5 ? l1_names[ty][tx] : "?"; }
void stream_tile_label(const stream_tile_t *t, char *buf, int n) {
    int sh = t->level >= STREAM_L_REGION ? t->level - STREAM_L_REGION : 0;   // owning L1 cell
    snprintf(buf, n, "L%d %d,%d (%s)", t->level, t->tx, t->ty, t->level == 0 ? "continent" : stream_l1_name(t->tx >> sh, t->ty >> sh));
}

// ---------------------------------------------------------------------------
// disk cache

typedef struct tile_header_t { unsigned magic, version, level, tx, ty, res, seed, reserved; } tile_header_t;

static void tile_path(const stream_tile_t *t, char *buf, size_t n) {
    snprintf(buf, n, "%s/L%d_%d_%d.mht", tile_dir, t->level, t->tx, t->ty);
}

static float *tile_disk_read(const stream_tile_t *t) {
    char path[320]; tile_path(t, path, sizeof path);
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    tile_header_t h; float *d = 0;
    int ok = fread(&h, sizeof h, 1, f) == 1 && h.magic == STREAM_MAGIC && h.version == STREAM_VERSION
          && h.level == (unsigned)t->level && h.tx == (unsigned)t->tx && h.ty == (unsigned)t->ty
          && h.res == STREAM_RES && h.seed == stream_seed;
    if (ok) { d = (float *)malloc(STREAM_PAYLOAD); ok = fread(d, 1, STREAM_PAYLOAD, f) == STREAM_PAYLOAD; }
    fclose(f);
    if (!ok) { free(d); return 0; }                 // stale/corrupt: regenerate (the write replaces it)
    return d;
}

static int tile_disk_write(const stream_tile_t *t, const float *d) {
    char path[320], tmp[328]; tile_path(t, path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb"); if (!f) return 0;
    tile_header_t h = { STREAM_MAGIC, STREAM_VERSION, (unsigned)t->level, (unsigned)t->tx, (unsigned)t->ty, STREAM_RES, stream_seed, 0 };
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(d, 1, STREAM_PAYLOAD, f) == STREAM_PAYLOAD;
    ok = (fclose(f) == 0) && ok;
    if (ok) { remove(path); ok = rename(tmp, path) == 0; }
    if (!ok) remove(tmp);
    return ok;
}

// ---------------------------------------------------------------------------
// generation: deterministic fbm octaves beyond the base map's resolution, faded out where the
// base ground is low or wet so nothing the simulation placed (harbours, towns, coasts) moves.

static float *tile_generate(const stream_tile_t *t) {
    float *d = (float *)malloc(STREAM_PAYLOAD);
    float amp = lvl_amp[t->level], freq = lvl_freq[t->level];
    if (amp <= 0.f) { memset(d, 0, STREAM_PAYLOAD); return d; }
    float lon0, lat1, deg; tile_geo(t, &lon0, &lat1, &deg);
    float step = deg / (STREAM_RES - 1);
    unsigned seed = stream_seed ^ (0x9E3779B1u * (unsigned)(t->level + 1));
    for (int j = 0; j < STREAM_RES; ++j) for (int i = 0; i < STREAM_RES; ++i) {
        float lon = lon0 + i * step, lat = lat1 - j * step;
        float h = world_height(lon2x(lon), lat2z(lat));
        float m = clampf((h - STREAM_FADE_H) / 2.5f, 0.f, 1.f);           // flat near the shore, full in the mountains
        float v = m > 0.f ? m * amp * (world_fbm(lon * freq, lat * freq, 2, seed) * 2.f - 1.f) : 0.f;
        d[j * STREAM_RES + i] = v;
    }
    return d;
}

// ---------------------------------------------------------------------------
// load pipeline (the asset.c discipline: state flips only on the MAIN lane)

typedef struct tile_job_t { stream_tile_t *t; float *payload; int from_disk; double t0, io_ms, gen_ms; } tile_job_t;

static void tile_stage_commit(void *p) {     // MAIN lane: publish
    tile_job_t *j = (tile_job_t *)p;
    stream_tile_t *t = j->t;
    t->delta = j->payload; t->state = TILE_READY; t->on_disk = 1;
    resident_bytes += STREAM_PAYLOAD;
    residency_gen++;
    if (j->from_disk) { st_disk_hits++; st_io_ms = j->io_ms; }
    else { st_generated++; st_gen_ms = j->gen_ms; }
    LOG_TRACE("stream", "tile L%d %d,%d ready (%s, io %.1f ms, gen %.1f ms)", t->level, t->tx, t->ty, j->from_disk ? "disk" : "generated", j->io_ms, j->gen_ms);
    free(j);
}

static void tile_stage_write(void *p) {      // IO lane: cache the generated payload, then commit
    tile_job_t *j = (tile_job_t *)p;
    if (!tile_disk_write(j->t, j->payload)) LOG_WARN("stream", "cannot write tile cache for L%d %d,%d in %s", j->t->level, j->t->tx, j->t->ty, tile_dir);
    jobs_submit(JOB_LANE_MAIN, tile_stage_commit, j, 0);
}

static void tile_stage_generate(void *p) {   // DECODE lane (low priority): build the delta field
    tile_job_t *j = (tile_job_t *)p;
    double t0 = diag_now();
    j->payload = tile_generate(j->t);
    j->gen_ms = (diag_now() - t0) * 1000;
    jobs_submit(JOB_LANE_IO, tile_stage_write, j, 0);
}

static void tile_stage_read(void *p) {       // IO lane: try the disk cache first
    tile_job_t *j = (tile_job_t *)p;
    double t0 = diag_now();
    j->payload = tile_disk_read(j->t);
    j->io_ms = (diag_now() - t0) * 1000;
    if (j->payload) { j->from_disk = 1; jobs_submit(JOB_LANE_MAIN, tile_stage_commit, j, 0); }
    else jobs_submit(JOB_LANE_DECODE, tile_stage_generate, j, 0);
}

static void tile_request(stream_tile_t *t) {
    if (t->state != TILE_EMPTY || t->water_only) return;
    tile_job_t *j = (tile_job_t *)calloc(1, sizeof *j);
    j->t = t; j->t0 = diag_now();
    t->state = TILE_QUEUED;
    jobs_submit(JOB_LANE_IO, tile_stage_read, j, 0);
}

// ---------------------------------------------------------------------------
// init / shutdown

void stream_init(unsigned seed, const char *dir) {
    if (initialized) stream_shutdown();
    stream_seed = seed;
    snprintf(tile_dir, sizeof tile_dir, "%s", dir && *dir ? dir : "tiles");
    stream_mkdir(tile_dir);
    const char *e = getenv("MARE_STREAM_MB");
    if (e && atoi(e) > 0) budget_bytes = (size_t)atoi(e) * 1024u * 1024u;
    ntiles = 0;
    for (int l = 0; l < STREAM_LEVELS; ++l) {
        int c = lvl_cells(l);
        grid_w[l] = (LM_W + c - 1) / c; grid_h[l] = (LM_H + c - 1) / c;
        lvl_off[l] = ntiles; ntiles += grid_w[l] * grid_h[l];
    }
    lvl_off[STREAM_LEVELS] = ntiles;
    tiles = (stream_tile_t *)calloc((size_t)ntiles, sizeof *tiles);
    for (int l = 0; l < STREAM_LEVELS; ++l)
        for (int ty = 0; ty < grid_h[l]; ++ty) for (int tx = 0; tx < grid_w[l]; ++tx) {
            stream_tile_t *t = tile_get(l, tx, ty);
            t->level = (unsigned char)l; t->tx = (short)tx; t->ty = (short)ty;
        }
    // water-only tiles never load: mark leaves from the landmask, then propagate upwards
    st_water = 0;
    { int c = lvl_cells(STREAM_LEVELS - 1);
      for (int ty = 0; ty < grid_h[STREAM_LEVELS - 1]; ++ty) for (int tx = 0; tx < grid_w[STREAM_LEVELS - 1]; ++tx) {
          int land = 0;
          for (int y = ty * c; y < mini((ty + 1) * c, LM_H) && !land; ++y)
              for (int x = tx * c; x < mini((tx + 1) * c, LM_W); ++x) if (landmask[y * LM_W + x]) { land = 1; break; }
          tile_get(STREAM_LEVELS - 1, tx, ty)->water_only = !land;
      } }
    for (int l = STREAM_LEVELS - 2; l >= 0; --l)
        for (int ty = 0; ty < grid_h[l]; ++ty) for (int tx = 0; tx < grid_w[l]; ++tx) {
            int water = 1;
            for (int dy = 0; dy < 2 && water; ++dy) for (int dx = 0; dx < 2; ++dx) {
                stream_tile_t *ch = tile_get(l + 1, tx * 2 + dx, ty * 2 + dy);
                if (ch && !ch->water_only) { water = 0; break; }
            }
            tile_get(l, tx, ty)->water_only = (unsigned char)water;
        }
    for (int i = 0; i < ntiles; ++i) st_water += tiles[i].water_only;
    resident_bytes = 0; st_evicted = st_disk_hits = st_generated = 0; update_no = 0; residency_gen = 1;
    initialized = 1;
    LOG_INFO("stream", "world streaming: %d tiles (L0 %dx%d .. L3 %dx%d), %d water-only, budget %zu MB, cache '%s'",
             ntiles, grid_w[0], grid_h[0], grid_w[3], grid_h[3], st_water, budget_bytes / (1024 * 1024), tile_dir);
}

void stream_shutdown(void) {
    if (!initialized) return;
    for (;;) { int busy = 0; for (int i = 0; i < ntiles; ++i) busy += tiles[i].state == TILE_QUEUED;
               if (!busy) break; jobs_help(); }
    for (int i = 0; i < ntiles; ++i) free(tiles[i].delta);
    free(tiles); tiles = 0; ntiles = 0; resident_bytes = 0; initialized = 0;
}

// ---------------------------------------------------------------------------
// wanted set (quadtree descent) + budget/eviction

static int inflight_started;

// Descend into a child only while the camera is within STREAM_SPLIT_K x the child's size of
// its centre: finer rings of tiles around the camera, coarser outward (centre distance, not
// rect distance - a rect metric makes every neighbour distance 0 and the whole grid splits).
static void want_node(int level, int tx, int ty, float cx, float cz, float dist) {
    stream_tile_t *t = tile_get(level, tx, ty);
    if (!t) return;
    t->wanted = 1; t->last_want = update_no;
    if (!t->water_only && t->state == TILE_EMPTY && inflight_started < STREAM_MAX_INFLIGHT) { tile_request(t); inflight_started++; }
    if (level >= STREAM_LEVELS - 1) return;
    float csize = lvl_cells(level + 1) / (float)CELLS_PER_DEG * WORLD_K;
    for (int dy = 0; dy < 2; ++dy) for (int dxx = 0; dxx < 2; ++dxx) {
        stream_tile_t *ch = tile_get(level + 1, tx * 2 + dxx, ty * 2 + dy);
        if (!ch) continue;
        float x0, z0, x1, z1; stream_tile_rect(ch, &x0, &z0, &x1, &z1);
        float dx = (x0 + x1) * 0.5f - cx, dz = (z0 + z1) * 0.5f - cz;
        float d = sqrtf(dx * dx + dz * dz + dist * dist);
        if (d < csize * STREAM_SPLIT_K)
            want_node(level + 1, tx * 2 + dxx, ty * 2 + dy, cx, cz, dist);
    }
}

static int tile_has_resident_children(const stream_tile_t *t) {
    if (t->level >= STREAM_LEVELS - 1) return 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const stream_tile_t *ch = tile_get(t->level + 1, t->tx * 2 + dx, t->ty * 2 + dy);
        if (ch && ch->state != TILE_EMPTY) return 1;
    }
    return 0;
}

void stream_update(float cx, float cz, float dist) {
    if (!initialized) return;
    update_no++;
    inflight_started = 0;
    for (int i = 0; i < ntiles; ++i) tiles[i].wanted = 0;
    for (int ty = 0; ty < grid_h[0]; ++ty) for (int tx = 0; tx < grid_w[0]; ++tx)
        want_node(0, tx, ty, cx, cz, dist);
    // unloading policy: never a wanted tile, never one whose children are resident; LRU first,
    // a few per update - eviction is never a stall, and the disk cache keeps the file.
    for (int k = 0; k < STREAM_EVICT_PER_UPDATE && resident_bytes > budget_bytes; ++k) {
        stream_tile_t *victim = 0;
        for (int i = 0; i < ntiles; ++i) {
            stream_tile_t *t = &tiles[i];
            if (t->state != TILE_READY || t->wanted || tile_has_resident_children(t)) continue;
            if (!victim || t->last_want < victim->last_want) victim = t;
        }
        if (!victim) break;
        free(victim->delta); victim->delta = 0; victim->state = TILE_EMPTY;
        resident_bytes -= STREAM_PAYLOAD; st_evicted++; residency_gen++;
        LOG_TRACE("stream", "evicted tile L%d %d,%d (last wanted %u updates ago)", victim->level, victim->tx, victim->ty, update_no - victim->last_want);
    }
}

// ---------------------------------------------------------------------------
// lookups and stats

float stream_delta_at(float wx, float wz) {
    if (!initialized) return 0.f;
    float lon = x2lon(wx), lat = z2lat(wz);
    for (int l = STREAM_LEVELS - 1; l >= 0; --l) {
        if (lvl_amp[l] <= 0.f) return 0.f;                       // L0/L1 carry no delta by design
        float deg = lvl_cells(l) / (float)CELLS_PER_DEG;
        int tx = (int)floorf((lon - LON0) / deg), ty = (int)floorf((LAT1 - lat) / deg);
        const stream_tile_t *t = tile_get(l, tx, ty);
        if (!t || t->state != TILE_READY || !t->delta) continue;
        float fx = (lon - (LON0 + tx * deg)) / deg * (STREAM_RES - 1);
        float fy = ((LAT1 - ty * deg) - lat) / deg * (STREAM_RES - 1);
        int x0 = mini(maxi((int)fx, 0), STREAM_RES - 2), y0 = mini(maxi((int)fy, 0), STREAM_RES - 2);
        float txf = clampf(fx - x0, 0.f, 1.f), tyf = clampf(fy - y0, 0.f, 1.f);
        const float *d = t->delta;
        float a = d[y0 * STREAM_RES + x0], b = d[y0 * STREAM_RES + x0 + 1];
        float c = d[(y0 + 1) * STREAM_RES + x0], e = d[(y0 + 1) * STREAM_RES + x0 + 1];
        return lerpf(lerpf(a, b, txf), lerpf(c, e, txf), tyf);
    }
    return 0.f;
}

unsigned stream_residency_generation(void) { return residency_gen; }
size_t   stream_budget_bytes(void) { return budget_bytes; }
void     stream_set_budget(size_t bytes) { if (bytes > 0) budget_bytes = bytes; }

void stream_set_authored(int level, int tx, int ty, int authored) {
    stream_tile_t *t = tile_get(level, tx, ty);
    if (t) t->authored = (unsigned char)(authored != 0);
}

void stream_stats(stream_stats_t *out) {
    memset(out, 0, sizeof *out);
    for (int i = 0; i < ntiles; ++i) {
        out->resident += tiles[i].state == TILE_READY;
        out->loading  += tiles[i].state == TILE_QUEUED;
        out->wanted   += tiles[i].wanted;
    }
    out->water = st_water;
    out->evicted = st_evicted; out->disk_hits = st_disk_hits; out->generated = st_generated;
    out->bytes = resident_bytes; out->budget_bytes = budget_bytes;
    out->last_gen_ms = st_gen_ms; out->last_io_ms = st_io_ms;
}
