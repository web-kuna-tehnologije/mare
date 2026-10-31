// World streaming (see stream.h, game/docs/DESIGN_STREAMING.md). Sections: grids and tile
// storage · disk cache (read/validate/write) · deterministic delta generation · the load
// pipeline on the job system (IO read -> DECODE generate -> IO write-back -> MAIN commit) ·
// wanted set (quadtree descent) · memory budget + LRU eviction · lookups and stats.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#ifdef _WIN32
#include <direct.h>
#define stream_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define stream_mkdir(p) mkdir(p, 0755)
#endif

extern "C" {
#include "stream.h"
#include "world.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
}

#define STREAM_MAGIC   0x4C49544Du   // 'MTIL'
#define STREAM_VERSION 1u
#define STREAM_PAYLOAD ((size_t)STREAM_RES * STREAM_RES * sizeof(float))

namespace mare::game::world {

typedef struct tile_header_t { unsigned magic, version, level, tx, ty, res, seed, reserved; } tile_header_t;
typedef struct tile_job_t { stream_tile_t *t; float *payload; int from_disk; double t0, io_ms, gen_ms; } tile_job_t;

class Stream {
public:
    void           init(unsigned seed, const char *dir);
    void           shutdown();
    void           update(float cx, float cz, float dist);
    int            grid_width(int level);
    int            grid_height(int level);
    int            tile_count();
    stream_tile_t *tile(int level, int tx, int ty);
    stream_tile_t *tile_at_index(int i);
    static void    tile_rect(const stream_tile_t *t, float *x0, float *z0, float *x1, float *z1);
    static const char *l1_name(int tx, int ty);
    static void    tile_label(const stream_tile_t *t, char *buf, int n);
    float          delta_at(float wx, float wz);
    float          delta_at_level(float wx, float wz, int max_level);
    unsigned       residency_generation();
    void           stats(stream_stats_t *out);
    size_t         budget();
    void           set_budget(size_t bytes);
    void           set_authored(int level, int tx, int ty, int authored);
    int            dump_chunk_reference(int level, int tx, int ty, int res, const char *path,
                                        stream_chunk_ref_t *out);

private:
    // detail bands per level: amplitude in world units and noise frequency in cycles per degree.
    // L0/L1 are at or below the base map's resolution, so they carry no delta (zero payload): they
    // exist as residency markers and coarse fallbacks, which keeps every code path uniform.
    static constexpr float lvl_amp[STREAM_LEVELS]  = { 0.f, 0.f, 0.28f, 0.48f };   // 0.22.5: 40% lower ("reduce close-by heights")
    static constexpr float lvl_freq[STREAM_LEVELS] = { 0.f, 0.f, 3.0f,  6.5f  };

    int   grid_w[STREAM_LEVELS] = {}, grid_h[STREAM_LEVELS] = {}, lvl_off[STREAM_LEVELS + 1] = {};
    stream_tile_t *tiles = nullptr;          // all levels, level-major
    int   ntiles = 0;
    unsigned stream_seed = 0, residency_gen = 1, update_no = 0;
    char  tile_dir[256] = {};
    size_t resident_bytes = 0, budget_bytes = 64u * 1024u * 1024u;
    int   st_evicted = 0, st_disk_hits = 0, st_generated = 0, st_water = 0;
    double st_gen_ms = 0, st_io_ms = 0;
    int   initialized = 0;
    int   inflight_started = 0;

    static int lvl_cells(int level);
    stream_tile_t *tile_get(int level, int tx, int ty);
    static void tile_geo(const stream_tile_t *t, float *lon0, float *lat1, float *deg);
    void   tile_path(const stream_tile_t *t, char *buf, size_t n);
    float *tile_disk_read(const stream_tile_t *t);
    int    tile_disk_write(const stream_tile_t *t, const float *d);
    float *tile_generate(const stream_tile_t *t);
    void   tile_stage_commit(void *p);
    void   tile_stage_write(void *p);
    void   tile_stage_generate(void *p);
    void   tile_stage_read(void *p);
    // jobs_submit entry points: plain void* callbacks that forward to the singleton
    static void cb_stage_commit(void *p);
    static void cb_stage_write(void *p);
    static void cb_stage_generate(void *p);
    static void cb_stage_read(void *p);
    void   tile_request(stream_tile_t *t);
    void   want_node(int level, int tx, int ty, float cx, float cz, float dist);
    int    tile_has_resident_children(const stream_tile_t *t);

    static constexpr const char *l1_names[5][6] = {
        { "Hudson Bay & Shield", "Great Lakes & Labrador", "Newfoundland & N Atlantic", "British Isles & Norway", "Baltic & Central Europe", "Muscovy & Black Sea N" },
        { "New Spain Pacific", "Gulf of Mexico & Florida", "Caribbean & Sargasso", "Azores & Canaries", "Mediterranean & Sahara", "Levant & Egypt" },
        { "Eastern Pacific", "Peru & New Granada", "Brazil & Amazon", "Mid-Atlantic", "Guinea & Congo coast", "East African rim" },
        { "South Pacific", "Chile & W Patagonia", "La Plata & E Patagonia", "South Atlantic", "Cape of Good Hope", "SW Indian Ocean" },
        { "Southern Ocean", "Southern Ocean", "Southern Ocean", "Southern Ocean", "Southern Ocean", "Southern Ocean" },
    };
};

Stream &stream() { static Stream s; return s; }

int Stream::lvl_cells(int level) { return STREAM_L0_CELLS >> level; }

stream_tile_t *Stream::tile_get(int level, int tx, int ty) {
    if (level < 0 || level >= STREAM_LEVELS || tx < 0 || ty < 0 || tx >= grid_w[level] || ty >= grid_h[level]) return 0;
    return &tiles[lvl_off[level] + ty * grid_w[level] + tx];
}
stream_tile_t *Stream::tile(int level, int tx, int ty) { return tile_get(level, tx, ty); }
stream_tile_t *Stream::tile_at_index(int i) { return i >= 0 && i < ntiles ? &tiles[i] : 0; }
int Stream::tile_count() { return ntiles; }
int Stream::grid_width(int level) { return level >= 0 && level < STREAM_LEVELS ? grid_w[level] : 0; }
int Stream::grid_height(int level) { return level >= 0 && level < STREAM_LEVELS ? grid_h[level] : 0; }

// nominal extent in degrees (edge tiles reach past the world; samples there clamp like the base map)
void Stream::tile_geo(const stream_tile_t *t, float *lon0, float *lat1, float *deg) {
    *deg  = lvl_cells(t->level) / (float)CELLS_PER_DEG;
    *lon0 = LON0 + t->tx * *deg;
    *lat1 = LAT1 - t->ty * *deg;                       // tiles count from the north-west corner
}
void Stream::tile_rect(const stream_tile_t *t, float *x0, float *z0, float *x1, float *z1) {
    float lon0, lat1, deg; tile_geo(t, &lon0, &lat1, &deg);
    *x0 = lon2x(lon0); *x1 = lon2x(minf(lon0 + deg, LON1));
    *z0 = lat2z(lat1); *z1 = lat2z(maxf(lat1 - deg, LAT0));
}

const char *Stream::l1_name(int tx, int ty) { return (unsigned)tx < 6 && (unsigned)ty < 5 ? l1_names[ty][tx] : "?"; }
void Stream::tile_label(const stream_tile_t *t, char *buf, int n) {
    int sh = t->level >= STREAM_L_REGION ? t->level - STREAM_L_REGION : 0;   // owning L1 cell
    snprintf(buf, n, "L%d %d,%d (%s)", t->level, t->tx, t->ty, t->level == 0 ? "continent" : l1_name(t->tx >> sh, t->ty >> sh));
}

// ---------------------------------------------------------------------------
// disk cache

void Stream::tile_path(const stream_tile_t *t, char *buf, size_t n) {
    snprintf(buf, n, "%s/L%d_%d_%d.mht", tile_dir, t->level, t->tx, t->ty);
}

float *Stream::tile_disk_read(const stream_tile_t *t) {
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

int Stream::tile_disk_write(const stream_tile_t *t, const float *d) {
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

float *Stream::tile_generate(const stream_tile_t *t) {
    float *d = (float *)malloc(STREAM_PAYLOAD);
    float amp = lvl_amp[t->level], freq = lvl_freq[t->level];
    if (amp <= 0.f) { memset(d, 0, STREAM_PAYLOAD); return d; }
    float lon0, lat1, deg; tile_geo(t, &lon0, &lat1, &deg);
    float step = deg / (STREAM_RES - 1);
    unsigned seed = stream_seed ^ (0x9E3779B1u * (unsigned)(t->level + 1));
    for (int j = 0; j < STREAM_RES; ++j) for (int i = 0; i < STREAM_RES; ++i) {
        float lon = lon0 + i * step, lat = lat1 - j * step;
        float h = world_height(lon2x(lon), lat2z(lat));
        float m = clampf((h - STREAM_FADE_H) / 1.5f, 0.f, 1.f);           // flat near the shore, full a unit and a half up
        float v = 0.f;
        if (m > 0.f) {
            // Ridged detail (0.17): the same crest-and-flank shape as the base relief, one and
            // two octaves finer, plus a little plain fbm so the ridges do not read as a pattern.
            // Grows with the ground's height so high country is the roughest.
            float r = 1.f - fabsf(2.f * world_fbm(lon * freq, lat * freq, 3, seed) - 1.f); r *= r;
            float f = world_fbm(lon * freq * 2.1f + 5, lat * freq * 2.1f + 9, 2, seed ^ 0x55u) * 2.f - 1.f;
            v = m * amp * (0.65f * (r * 2.f - 1.f) + 0.35f * f) * (0.5f + 0.5f * clampf(h / 6.f, 0.f, 1.f));
        }
        d[j * STREAM_RES + i] = v;
    }
    return d;
}

// ---------------------------------------------------------------------------
// load pipeline (the asset.c discipline: state flips only on the MAIN lane)

void Stream::cb_stage_commit(void *p) { stream().tile_stage_commit(p); }
void Stream::cb_stage_write(void *p) { stream().tile_stage_write(p); }
void Stream::cb_stage_generate(void *p) { stream().tile_stage_generate(p); }
void Stream::cb_stage_read(void *p) { stream().tile_stage_read(p); }

void Stream::tile_stage_commit(void *p) {    // MAIN lane: publish
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

void Stream::tile_stage_write(void *p) {     // IO lane: cache the generated payload, then commit
    tile_job_t *j = (tile_job_t *)p;
    if (!tile_disk_write(j->t, j->payload)) LOG_WARN("stream", "cannot write tile cache for L%d %d,%d in %s", j->t->level, j->t->tx, j->t->ty, tile_dir);
    jobs_submit(JOB_LANE_MAIN, cb_stage_commit, j, 0);
}

void Stream::tile_stage_generate(void *p) {  // DECODE lane (low priority): build the delta field
    tile_job_t *j = (tile_job_t *)p;
    double t0 = diag_now();
    j->payload = tile_generate(j->t);
    j->gen_ms = (diag_now() - t0) * 1000;
    jobs_submit(JOB_LANE_IO, cb_stage_write, j, 0);
}

void Stream::tile_stage_read(void *p) {      // IO lane: try the disk cache first
    tile_job_t *j = (tile_job_t *)p;
    double t0 = diag_now();
    j->payload = tile_disk_read(j->t);
    j->io_ms = (diag_now() - t0) * 1000;
    if (j->payload) { j->from_disk = 1; jobs_submit(JOB_LANE_MAIN, cb_stage_commit, j, 0); }
    else jobs_submit(JOB_LANE_DECODE, cb_stage_generate, j, 0);
}

void Stream::tile_request(stream_tile_t *t) {
    if (t->state != TILE_EMPTY || t->water_only) return;
    tile_job_t *j = (tile_job_t *)calloc(1, sizeof *j);
    j->t = t; j->t0 = diag_now();
    t->state = TILE_QUEUED;
    jobs_submit(JOB_LANE_IO, cb_stage_read, j, 0);
}

// ---------------------------------------------------------------------------
// init / shutdown

void Stream::init(unsigned seed, const char *dir) {
    if (initialized) shutdown();
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

void Stream::shutdown() {
    if (!initialized) return;
    for (;;) { int busy = 0; for (int i = 0; i < ntiles; ++i) busy += tiles[i].state == TILE_QUEUED;
               if (!busy) break; jobs_help(); }
    for (int i = 0; i < ntiles; ++i) free(tiles[i].delta);
    free(tiles); tiles = 0; ntiles = 0; resident_bytes = 0; initialized = 0;
}

// ---------------------------------------------------------------------------
// wanted set (quadtree descent) + budget/eviction

// Descend into a child only while the camera is within STREAM_SPLIT_K x the child's size of
// its centre: finer rings of tiles around the camera, coarser outward (centre distance, not
// rect distance - a rect metric makes every neighbour distance 0 and the whole grid splits).
void Stream::want_node(int level, int tx, int ty, float cx, float cz, float dist) {
    stream_tile_t *t = tile_get(level, tx, ty);
    if (!t) return;
    t->wanted = 1; t->last_want = update_no;
    if (!t->water_only && t->state == TILE_EMPTY && inflight_started < STREAM_MAX_INFLIGHT) { tile_request(t); inflight_started++; }
    if (level >= STREAM_LEVELS - 1) return;
    float csize = lvl_cells(level + 1) / (float)CELLS_PER_DEG * WORLD_K;
    for (int dy = 0; dy < 2; ++dy) for (int dxx = 0; dxx < 2; ++dxx) {
        stream_tile_t *ch = tile_get(level + 1, tx * 2 + dxx, ty * 2 + dy);
        if (!ch) continue;
        float x0, z0, x1, z1; tile_rect(ch, &x0, &z0, &x1, &z1);
        float dx = (x0 + x1) * 0.5f - cx, dz = (z0 + z1) * 0.5f - cz;
        float d = sqrtf(dx * dx + dz * dz + dist * dist);
        if (d < csize * STREAM_SPLIT_K)
            want_node(level + 1, tx * 2 + dxx, ty * 2 + dy, cx, cz, dist);
    }
}

int Stream::tile_has_resident_children(const stream_tile_t *t) {
    if (t->level >= STREAM_LEVELS - 1) return 0;
    for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
        const stream_tile_t *ch = tile_get(t->level + 1, t->tx * 2 + dx, t->ty * 2 + dy);
        if (ch && ch->state != TILE_EMPTY) return 1;
    }
    return 0;
}

void Stream::update(float cx, float cz, float dist) {
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

float Stream::delta_at(float wx, float wz) { return delta_at_level(wx, wz, STREAM_LEVELS - 1); }

float Stream::delta_at_level(float wx, float wz, int max_level) {
    if (!initialized) return 0.f;
    if (max_level > STREAM_LEVELS - 1) max_level = STREAM_LEVELS - 1;
    float lon = x2lon(wx), lat = z2lat(wz);
    for (int l = max_level; l >= 0; --l) {
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

unsigned Stream::residency_generation() { return residency_gen; }
size_t   Stream::budget() { return budget_bytes; }
void     Stream::set_budget(size_t bytes) { if (bytes > 0) budget_bytes = bytes; }

void Stream::set_authored(int level, int tx, int ty, int authored) {
    stream_tile_t *t = tile_get(level, tx, ty);
    if (t) t->authored = (unsigned char)(authored != 0);
}

// ---------------------------------------------------------------------------
// chunk reference image (docs/BLENDER_LOD.md section 4): what a map chunk is modelled from.
// Shaded relief, sun from the north-west, sea by depth and land by height - the same reading
// the map view gives, at a resolution a text-to-3D or image-to-3D service can work with.

int Stream::dump_chunk_reference(int level, int tx, int ty, int res, const char *path,
                                 stream_chunk_ref_t *out) {
    if (!initialized) { LOG_ERROR("stream", "stream_init must run before a chunk reference"); return 0; }
    const stream_tile_t *t = tile_get(level, tx, ty);
    if (!t) { LOG_ERROR("stream", "no tile L%d %d,%d (level grid is %dx%d)", level, tx, ty,
                        grid_width(level), grid_height(level)); return 0; }
    if (res < 16) res = 16;
    float lon0, lat1, deg; tile_geo(t, &lon0, &lat1, &deg);
    stream_chunk_ref_t r;
    r.lon0 = lon0; r.lat1 = lat1; r.lon1 = lon0 + deg; r.lat0 = lat1 - deg;
    tile_rect(t, &r.x0, &r.z0, &r.x1, &r.z1);
    r.size_deg = deg; r.size_units = deg * WORLD_K;
    r.min_h = 1e9f; r.max_h = -1e9f; r.land_frac = 0;

    unsigned char *px = (unsigned char *)malloc((size_t)res * res * 3);
    if (!px) return 0;
    float step = deg / (res - 1);
    int land = 0;
    for (int j = 0; j < res; ++j) for (int i = 0; i < res; ++i) {
        float lon = lon0 + i * step, lat = lat1 - j * step;
        float wx = lon2x(lon), wz = lat2z(lat);
        float h = world_height(wx, wz) + delta_at(wx, wz);
        if (h < r.min_h) r.min_h = h;
        if (h > r.max_h) r.max_h = h;
        int is_land = world_is_land(wx, wz); land += is_land;
        // slope shading from the two neighbours, sun from the north-west
        float e = step * 0.5f;
        float hx = world_height(lon2x(lon + e), wz) - world_height(lon2x(lon - e), wz);
        float hz = world_height(wx, lat2z(lat - e)) - world_height(wx, lat2z(lat + e));
        float sh = clampf(0.62f + 0.9f * (-hx - hz) / (e * WORLD_K * 4.f + 1e-4f), 0.25f, 1.35f);
        float cr, cg, cb;
        if (!is_land || h < 0.02f) {                       // sea: deeper is darker and bluer
            float d = clampf(-h / 3.f, 0.f, 1.f);
            cr = lerpf(0.42f, 0.05f, d); cg = lerpf(0.60f, 0.16f, d); cb = lerpf(0.74f, 0.34f, d);
            sh = 1.f;
        } else if (h < 0.35f) { cr = 0.84f; cg = 0.78f; cb = 0.58f;             // beach
        } else if (h < 3.f)   { float u = (h - 0.35f) / 2.65f;                  // grass -> scrub
                                cr = lerpf(0.33f, 0.52f, u); cg = lerpf(0.50f, 0.47f, u); cb = lerpf(0.22f, 0.29f, u);
        } else if (h < 7.f)   { float u = (h - 3.f) / 4.f;                      // rock
                                cr = lerpf(0.52f, 0.49f, u); cg = lerpf(0.47f, 0.45f, u); cb = lerpf(0.29f, 0.43f, u);
        } else                { float u = clampf((h - 7.f) / 3.f, 0.f, 1.f);    // snow
                                cr = lerpf(0.49f, 0.95f, u); cg = lerpf(0.45f, 0.96f, u); cb = lerpf(0.43f, 0.98f, u); }
        unsigned char *p = px + ((size_t)j * res + i) * 3;
        p[0] = (unsigned char)(clampf(cr * sh, 0, 1) * 255);
        p[1] = (unsigned char)(clampf(cg * sh, 0, 1) * 255);
        p[2] = (unsigned char)(clampf(cb * sh, 0, 1) * 255);
    }
    r.land_frac = land / (float)(res * res);
    FILE *f = fopen(path, "wb");
    if (!f) { LOG_ERROR("stream", "cannot write chunk reference %s", path); free(px); return 0; }
    fprintf(f, "P6\n%d %d\n255\n", res, res);
    size_t wrote = fwrite(px, 1, (size_t)res * res * 3, f);
    fclose(f); free(px);
    if (wrote != (size_t)res * res * 3) { LOG_ERROR("stream", "short write to %s", path); return 0; }
    LOG_INFO("stream", "chunk reference L%d %d,%d -> %s (%dx%d): lon %.2f..%.2f lat %.2f..%.2f, "
             "%.0f world units square, height %.2f..%.2f, %.0f%% land",
             level, tx, ty, path, res, res, r.lon0, r.lon1, r.lat0, r.lat1,
             r.size_units, r.min_h, r.max_h, r.land_frac * 100);
    if (out) *out = r;
    return 1;
}

void Stream::stats(stream_stats_t *out) {
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

} // namespace mare::game::world

// C ABI (stream.h) for the unported callers
extern "C" {
void stream_init(unsigned seed, const char *tile_dir) { mare::game::world::stream().init(seed, tile_dir); }
void stream_shutdown(void) { mare::game::world::stream().shutdown(); }
void stream_update(float cx, float cz, float dist) { mare::game::world::stream().update(cx, cz, dist); }
int  stream_grid_w(int level) { return mare::game::world::stream().grid_width(level); }
int  stream_grid_h(int level) { return mare::game::world::stream().grid_height(level); }
int  stream_tile_count(void) { return mare::game::world::stream().tile_count(); }
stream_tile_t *stream_tile(int level, int tx, int ty) { return mare::game::world::stream().tile(level, tx, ty); }
stream_tile_t *stream_tile_at_index(int i) { return mare::game::world::stream().tile_at_index(i); }
void stream_tile_rect(const stream_tile_t *t, float *x0, float *z0, float *x1, float *z1) { mare::game::world::Stream::tile_rect(t, x0, z0, x1, z1); }
const char *stream_l1_name(int tx, int ty) { return mare::game::world::Stream::l1_name(tx, ty); }
void stream_tile_label(const stream_tile_t *t, char *buf, int n) { mare::game::world::Stream::tile_label(t, buf, n); }
float    stream_delta_at(float wx, float wz) { return mare::game::world::stream().delta_at(wx, wz); }
float    stream_delta_at_level(float wx, float wz, int max_level) { return mare::game::world::stream().delta_at_level(wx, wz, max_level); }
unsigned stream_residency_generation(void) { return mare::game::world::stream().residency_generation(); }
void     stream_stats(stream_stats_t *out) { mare::game::world::stream().stats(out); }
size_t   stream_budget_bytes(void) { return mare::game::world::stream().budget(); }
void     stream_set_budget(size_t bytes) { mare::game::world::stream().set_budget(bytes); }
void stream_set_authored(int level, int tx, int ty, int authored) { mare::game::world::stream().set_authored(level, tx, ty, authored); }
int stream_dump_chunk_reference(int level, int tx, int ty, int res, const char *path,
                                stream_chunk_ref_t *out) {
    return mare::game::world::stream().dump_chunk_reference(level, tx, ty, res, path, out);
}
}
