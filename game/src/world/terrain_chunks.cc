// The terrain export read as chunks (see terrain_chunks.h and assets/data_new/MARE_INTEGRATION.md).
// Sections: datasets and manifests · the load pipeline on the job system · the wanted set and
// the budget · sampling · the one-shot geography fold · the C ABI.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

extern "C" {
#include "terrain_chunks.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/jobs.h"
#include "../assets/asset.h"
}
#if !defined(STBI_INCLUDE_STB_IMAGE_H)
#include "../../../ext/ext-image/ext-image/3rd/stb_image.h"
#endif

namespace mare::game::world {

// The calibration the whole game is tuned against: land linear to 9000 m -> 20 world units,
// the sea sqrt-compressed to 12000 m -> -9. The sqrt on the sea half is deliberate: a linear
// real-metre shelf puts the whole north Adriatic at ~0.04 units and washes every coastal sea
// into the shallow colour. Every shallow-water gate, wave fade and biome threshold in the
// shaders assumes these numbers.
#define GC_LAND_MAX_M   9000.f
#define GC_DEPTH_MAX_M 12000.f
// 0.22.6 (issue3 "reduce the height of the terrain when zoomed in - the Po valley looks like
// Switzerland and the Alps like spikes"): 20 -> 16. A *zoom-dependent* scale was the obvious
// reading, and is wrong: every height threshold in the terrain shader - snow line, tree line,
// beach, scree - and every prop the scatter stands on the ground would move as the camera came
// in. So the whole world is 20% lower instead, which leaves the Po plain flat and takes the
// worst of the spikes off the ranges. MARE_HEIGHT_SCALE overrides it for a look without a build.
#define GC_HEIGHT_SCALE   gc_height_scale()
static float gc_height_scale_v = 16.f;
static inline float gc_height_scale(void) { return gc_height_scale_v; }
#define GC_DEPTH_SCALE     9.f
#define GC_M_PER_DEG  111320.f      // metres per degree of latitude: the export's normals use geographic metre spacing
// The high ranges (0.22.3). The linear 9000 m -> 20 units puts the Alps at 7-9 units, which
// from the Po plain reads as a line of hills; above GC_PEAK_FROM_M the land gains up to
// GC_PEAK_BOOST more, so the massifs stand over the valleys the way the eye expects while
// the lowlands and every coast keep their calibration (the boost is zero below 700 m).
#define GC_PEAK_FROM_M   700.f
#define GC_PEAK_FULL_M  2600.f
#define GC_PEAK_BOOST    0.28f

static inline float gc_world(float m) {
    if (m >= 0.f) {
        float t = (m - GC_PEAK_FROM_M) / (GC_PEAK_FULL_M - GC_PEAK_FROM_M);
        t = t <= 0.f ? 0.f : t >= 1.f ? 1.f : t * t * (3.f - 2.f * t);
        return minf(m / GC_LAND_MAX_M, 1.f) * GC_HEIGHT_SCALE * (1.f + GC_PEAK_BOOST * t);
    }
    return -sqrtf(minf(-m / GC_DEPTH_MAX_M, 1.f)) * GC_DEPTH_SCALE;
}

enum { GC_EMPTY = 0, GC_QUEUED, GC_READY };

typedef struct gc_chunk_t {
    unsigned char state;              // GC_EMPTY / GC_QUEUED / GC_READY
    unsigned char absent;             // the files are not there: never ask again
    short         cx, cy;
    float        *height;             // GC_SAMPLES^2 metres when ready
    unsigned char *coverage;          // N^2, 1 = valid sample
    unsigned char *cls, *splat, *normal;   // N^2 RGBA8 each (terrain_chunks.h), NULL when the files were missing
    unsigned      last_want;
    size_t        bytes;
} gc_chunk_t;

typedef struct gc_job_t {
    int ds; gc_chunk_t *c;
    float *height; unsigned char *coverage, *biome, *source, *coast;
    unsigned char *splat_png, *normal_png; size_t splat_n, normal_n;
    unsigned char *cls, *splat, *normal;
    double ms;
} gc_job_t;

typedef struct gc_ds_t {
    gc_dataset_t info;
    gc_chunk_t  *chunk; int nchunks;
    double       area;                // bbox area in square degrees (the overview is the largest)
} gc_ds_t;

#define GC_BAKE_SLOTS 64        // worker slots the bake's per-slot counters cover
class GeoChunks;
// one chunk row's worth of work for the geography fold, shared by the workers folding it
typedef struct bake_band_t {
    const gc_ds_t *d;
    float **row; unsigned char **cov;
    float *hm; unsigned char *lm; const unsigned char *protect;
    int w, h, cy, y0;
    float lon0, lat1, cells_per_deg, gw, gh;
    int allow_flips;
    int tally[GC_BAKE_SLOTS][2];   // [0] cells written, [1] new land
} bake_band_t;

typedef struct gc_biome_t { int id; const char *name; unsigned char rgb[3]; char name_buf[32]; } gc_biome_t;

class GeoChunks {
public:
    int      init(const char *dir);
    void     shutdown();
    void     update(float lon, float lat, float radius_deg);
    int      height_m(float lon, float lat, float *out);
    int      height_world(float lon, float lat, float *out);
    int      sample(float lon, float lat, gc_sample_t *out);
    int      resident(float lon, float lat);
    int      bake(float *hm, unsigned char *lm, int w, int h, float lon0, float lat1,
                  float cells_per_deg, const unsigned char *protect, int *flips);
    static void bake_rows(void *arg, int begin, int end, int slot);   // one band, on the pool
    void     stats(gc_stats_t *out);
    unsigned generation() { return gen; }
    unsigned update_number() { return update_no; }
    int      dataset_count() { return (int)ds.size(); }
    int      dataset(int i, gc_dataset_t *out) { if (i < 0 || i >= (int)ds.size()) return 0; *out = ds[i].info; return 1; }
    int      chunk_maps(int d, int cx, int cy, const unsigned char **cls, const unsigned char **splat, const unsigned char **normal);
    unsigned chunk_wanted(int d, int cx, int cy) { gc_chunk_t *c = at(d, cx, cy); return c ? c->last_want : 0; }
    const gc_biome_t *biome(int id);
    float    exaggeration() { return (GC_HEIGHT_SCALE / GC_LAND_MAX_M) / (WORLD_K / GC_M_PER_DEG); }

    int   present = 0, inited = 0;
    std::vector<gc_ds_t> ds;          // finest first
private:
    size_t bytes = 0, budget = (size_t)GC_BUDGET_MB * 1024 * 1024;
    unsigned update_no = 0, gen = 1;
    int loaded_total = 0, evicted = 0, missing_files = 0;
    double last_read_ms = 0;
    gc_biome_t biomes[16]; int nbiomes = 0;

    static int  json_num(const char *s, const char *key, double *out);
    static int  json_pair(const char *s, const char *key, double *a, double *b);
    static int  json_array(const char *s, const char *key, double *v, int n);
    static int  json_str(const char *s, const char *key, char *out, size_t n);
    static int  json_bool(const char *s, const char *key, int *out);
    static char *read_text(const char *path, size_t *n);
    static int  file_exists(const char *path) { FILE *f = fopen(path, "rb"); if (f) { fclose(f); return 1; } return 0; }
    void probe(const char *dir, std::vector<std::string> &roots);
    int  add_dataset(const char *root);
    void parse_biomes(const char *js);
    void default_biomes();
    gc_chunk_t *at(int d, int cx, int cy) {
        if (d < 0 || d >= (int)ds.size()) return nullptr;
        gc_ds_t &D = ds[d];
        return (unsigned)cx < (unsigned)D.info.chunks_x && (unsigned)cy < (unsigned)D.info.chunks_y ? &D.chunk[cy * D.info.chunks_x + cx] : nullptr; }
    int  locate(const gc_ds_t &D, float lon, float lat, int *cx, int *cy, float *fx, float *fy);
    const gc_chunk_t *find(float lon, float lat, int *dsi, float *fx, float *fy);
    void request(int d, gc_chunk_t *c);
    void publish(void *p);
    static void publish_cb(void *p);
    static void decode_cb(void *p);
    static void read_cb(void *p);
};

GeoChunks &geochunks() { static GeoChunks g; return g; }

// ---------------------------------------------------------------------------
// datasets and manifests

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
// "key": [ a, b, ... ] - the manifest writes its arrays over several lines
int GeoChunks::json_array(const char *s, const char *key, double *v, int n) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, '['); if (!p) return 0;
    const char *q = p + 1; char *end = nullptr;
    for (int i = 0; i < n; ++i) {
        v[i] = strtod(q, &end);
        if (end == q) return 0;
        q = strchr(end, ','); if (!q && i < n - 1) return 0;
        if (q) q++;
    }
    return 1;
}
int GeoChunks::json_pair(const char *s, const char *key, double *a, double *b) { double v[2]; if (!json_array(s, key, v, 2)) return 0; *a = v[0]; *b = v[1]; return 1; }
int GeoChunks::json_str(const char *s, const char *key, char *out, size_t n) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p + strlen(pat), ':'); if (!p) return 0;
    p = strchr(p, '"'); if (!p) return 0;
    const char *e = strchr(p + 1, '"'); if (!e) return 0;
    size_t len = (size_t)(e - p - 1); if (len >= n) len = n - 1;
    memcpy(out, p + 1, len); out[len] = 0;
    return 1;
}
int GeoChunks::json_bool(const char *s, const char *key, int *out) {
    char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(s, pat); if (!p) return 0;
    p = strchr(p, ':'); if (!p) return 0;
    while (*p == ':' || *p == ' ' || *p == '\t') p++;
    *out = !strncmp(p, "true", 4); return 1;
}

// Where the datasets live. The asset root is the per-worktree tree (assets/elm) and the
// export sits beside it under assets/data_new, so the first probe is the sibling; the rest let
// the game find it from a build directory or a staged copy, as asset_root() does. A root is
// either a dataset itself (manifest.json) or a folder of datasets.
void GeoChunks::probe(const char *dir, std::vector<std::string> &roots) {
    std::vector<std::string> cands;
    if (dir) cands.push_back(dir);
    else {
        const char *env = getenv("MARE_GEOCHUNK_DIR");
        if (env) cands.push_back(env);
        else {
            // the newest delivery first (data_new_2: the LOD package and the regional builds),
            // then the previous one; every dataset found in either is registered
            static const char *names[] = { "data_chunks_terrain", "data_new_2", "data_new" };   // data_chunks_terrain: the world-game-package delivery (0.22.4)
            char buf[400];
            for (const char *nm : names) {
                snprintf(buf, sizeof buf, "%s/../%s", asset_root(), nm); cands.push_back(buf);
                snprintf(buf, sizeof buf, "%s/%s", asset_root(), nm);    cands.push_back(buf);
                static const char *plain[] = { "assets/%s", "../assets/%s", "../../assets/%s", "../../../assets/%s" };
                for (const char *p : plain) { snprintf(buf, sizeof buf, p, nm); cands.push_back(buf); }
            }
        }
    }
    std::vector<std::string> seen;
    for (const std::string &c : cands) {
        std::error_code ec;
        std::string canon = std::filesystem::weakly_canonical(c, ec).string(); if (ec) canon = c;
        if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
        std::string m = c + "/manifest.json";
        if (file_exists(m.c_str())) { roots.push_back(c); seen.push_back(canon); continue; }
        if (!std::filesystem::is_directory(c, ec)) continue;
        std::vector<std::string> found;
        for (const auto &e : std::filesystem::directory_iterator(c, ec)) {
            if (!e.is_directory(ec)) continue;
            std::string mm = e.path().string() + "/manifest.json";
            if (file_exists(mm.c_str())) found.push_back(e.path().string());
        }
        if (!found.empty()) { std::sort(found.begin(), found.end()); roots.insert(roots.end(), found.begin(), found.end()); seen.push_back(canon); }
    }
}

void GeoChunks::default_biomes() {
    // the spec's classes with the ESA WorldCover palette; the manifest's own table overrides
    static const gc_biome_t def[] = {
        { 0, "missing", { 0, 0, 0 }, "" }, { 1, "ocean", { 18, 55, 94 }, "" }, { 10, "tree_cover", { 37, 88, 43 }, "" },
        { 20, "shrubland", { 113, 127, 65 }, "" }, { 30, "grassland", { 126, 157, 73 }, "" }, { 40, "cropland", { 206, 173, 93 }, "" },
        { 50, "built_up", { 160, 80, 70 }, "" }, { 60, "bare_sparse", { 180, 170, 150 }, "" }, { 70, "snow_ice", { 240, 240, 240 }, "" },
        { 80, "permanent_water", { 30, 90, 160 }, "" }, { 90, "wetland", { 70, 130, 120 }, "" }, { 95, "mangroves", { 20, 120, 80 }, "" },
        { 100, "moss_lichen", { 170, 170, 120 }, "" },
    };
    nbiomes = (int)(sizeof def / sizeof def[0]);
    for (int i = 0; i < nbiomes; ++i) biomes[i] = def[i];
}
void GeoChunks::parse_biomes(const char *js) {
    const char *b = strstr(js, "\"biome_classes\""); if (!b) return;
    for (int i = 0; i < nbiomes; ++i) {
        char pat[16]; snprintf(pat, sizeof pat, "\"%d\"", biomes[i].id);
        const char *p = strstr(b, pat); if (!p) continue;
        const char *brace = strchr(p, '{'), *close = strchr(p, '}'); if (!brace || !close || brace > close) continue;
        char obj[256]; size_t n = (size_t)(close - brace + 1); if (n >= sizeof obj) n = sizeof obj - 1;
        memcpy(obj, brace, n); obj[n] = 0;
        if (json_str(obj, "name", biomes[i].name_buf, sizeof biomes[i].name_buf)) biomes[i].name = biomes[i].name_buf;
        double rgb[3]; if (json_array(obj, "rgb", rgb, 3)) for (int k = 0; k < 3; ++k) biomes[i].rgb[k] = (unsigned char)clampf((float)rgb[k], 0.f, 255.f);
    }
}
const gc_biome_t *GeoChunks::biome(int id) {
    for (int i = 0; i < nbiomes; ++i) if (biomes[i].id == id) return &biomes[i];
    return &biomes[0];
}

int GeoChunks::add_dataset(const char *root) {
    char path[400]; snprintf(path, sizeof path, "%s/manifest.json", root);
    size_t n = 0; char *js = read_text(path, &n);
    if (!js) { LOG_DEBUG("terrain", "no %s", path); return 0; }
    char fmt[48] = "";
    json_str(js, "format", fmt, sizeof fmt);
    int package = !strcmp(fmt, "mare-world-package-v1");
    if (!package && strcmp(fmt, "mare-terrain-v2")) {
        LOG_WARN("terrain", "%s: format \"%s\" is neither mare-terrain-v2 nor mare-world-package-v1 - dataset skipped", path, fmt);
        free(js); return 0;
    }
    double v4[4] = { -180, -90, 180, 90 };
    json_array(js, "bbox", v4, 4);
    int wrap = 0; json_bool(js, "longitude_wrap", &wrap);
    if (ds.empty()) {   // the class table is global to the format; the first manifest supplies it (a package keeps it in source-manifest.json)
        parse_biomes(js);
        if (package) { char sp[400]; snprintf(sp, sizeof sp, "%s/source-manifest.json", root);
                       size_t sn = 0; char *sj = read_text(sp, &sn); if (sj) { parse_biomes(sj); free(sj); } }
    }
    // A plain v2 export is one level; a package lists its levels under "lod_levels", each with
    // its own cells / samples / step and chunk directory. Every level becomes a dataset of its
    // own: the finest answers first, the coarser ones stand in while it streams and feed the
    // far ring where their spacing is all it can carry.
    int added = 0;
    const char *lv = package ? strstr(js, "\"lod_levels\"") : nullptr;
    const char *cursor = lv ? strchr(lv, '[') : js;
    for (int level = 0; level < 8; ++level) {
        const char *obj = cursor;
        char buf[2048]; const char *src = js;
        if (package) {
            obj = strchr(cursor, '{'); if (!obj) break;
            // the level object runs to its matching brace
            int depth = 0; const char *e = obj;
            for (; *e; ++e) { if (*e == '{') depth++; else if (*e == '}' && --depth == 0) break; }
            if (!*e) break;
            size_t n = (size_t)(e - obj + 1); if (n >= sizeof buf) n = sizeof buf - 1;
            memcpy(buf, obj, n); buf[n] = 0; src = buf; cursor = e + 1;
            if (!strstr(buf, "\"level\"")) break;          // past the lod_levels array: the geometry / maps objects follow
        }
        gc_ds_t D = {}; gc_dataset_t &I = D.info;
        double a = 0, b = 0, samples = GC_SAMPLES, cells = GC_CELLS, sx = 0, sy = 0, lvl = 0;
        int ok = json_pair(src, "chunk_count", &a, &b);
        json_num(src, "samples_per_chunk", &samples);
        json_num(src, "cells_per_chunk", &cells);
        json_pair(src, "pixel_step_degrees", &sx, &sy);
        if (package) json_num(src, "level", &lvl);
        if (!ok) { LOG_WARN("terrain", "%s: no chunk_count", path); if (!package) break; else continue; }
        I.chunks_x = (int)a; I.chunks_y = (int)b; I.samples = (int)samples; I.cells = (int)cells; I.lod = (int)lvl;
        I.west = (float)v4[0]; I.south = (float)v4[1]; I.east = (float)v4[2]; I.north = (float)v4[3];
        I.wrap = wrap && fabsf((I.east - I.west) - 360.f) < 1e-3f;
        if (I.chunks_x <= 0 || I.chunks_y <= 0 || I.chunks_x * I.chunks_y > GC_MAX_CHUNKS) {
            LOG_WARN("terrain", "%s: %dx%d chunks is out of range (max %d)", path, I.chunks_x, I.chunks_y, GC_MAX_CHUNKS); if (!package) break; else continue; }
        if (I.samples != I.cells + 1 || I.samples < 3 || I.samples > GC_SAMPLES) {
            LOG_WARN("terrain", "%s: %d samples / %d cells per chunk is not a grid this build reads", path, I.samples, I.cells); if (!package) break; else continue; }
        I.step_lon = sx > 0 ? (float)sx : (I.east - I.west) / (I.chunks_x * I.cells);
        I.step_lat = sy > 0 ? (float)sy : (I.north - I.south) / (I.chunks_y * I.cells);
        snprintf(I.root, sizeof I.root, "%s", root);
        if (package) snprintf(I.subdir, sizeof I.subdir, "lods/%d/", I.lod); else I.subdir[0] = 0;
        D.area = (double)(I.east - I.west) * (I.north - I.south);
        D.nchunks = I.chunks_x * I.chunks_y;
        D.chunk = (gc_chunk_t *)calloc((size_t)D.nchunks, sizeof *D.chunk);
        for (int y = 0; y < I.chunks_y; ++y) for (int x = 0; x < I.chunks_x; ++x) { gc_chunk_t *c = &D.chunk[y * I.chunks_x + x]; c->cx = (short)x; c->cy = (short)y; }
        {   // the spec derives a chunk's bounds from the manifest and every chunk ships its own
            // tile.json: read one and check the two agree, so a future layout is caught here
            snprintf(path, sizeof path, "%s/%schunks/0/0/tile.json", root, I.subdir);
            size_t tn = 0; char *tj = read_text(path, &tn);
            if (tj) {
                double t4[4];
                if (json_array(tj, "bounds_wgs84", t4, 4)) {
                    float e = I.west + (I.east - I.west) / I.chunks_x, s2 = I.north - (I.north - I.south) / I.chunks_y;
                    float d = fabsf((float)t4[0] - I.west) + fabsf((float)t4[1] - s2) + fabsf((float)t4[2] - e) + fabsf((float)t4[3] - I.north);
                    if (d > 1e-3f) LOG_WARN("terrain", "%s: chunk 0,0 says %.3f,%.3f..%.3f,%.3f but the manifest grid gives %.3f,%.3f..%.3f,%.3f", root, t4[0], t4[1], t4[2], t4[3], I.west, s2, e, I.north);
                }
                free(tj);
            }
        }
        ds.push_back(D); added++;
        LOG_INFO("terrain", "%s%s: %dx%d chunks of %d samples, %.4f x %.4f deg/sample (~%.0f m), lon %.1f..%.1f lat %.1f..%.1f%s, %s",
                 package ? "world-package level " : "terrain-v2 dataset", package ? std::to_string(I.lod).c_str() : "",
                 I.chunks_x, I.chunks_y, I.samples, I.step_lon, I.step_lat, I.step_lat * GC_M_PER_DEG, I.west, I.east, I.south, I.north, I.wrap ? ", wraps" : "", root);
        if (!package) break;
        if ((int)ds.size() >= GC_MAX_DATASETS) break;
    }
    free(js);
    return added;
}

int GeoChunks::init(const char *dir) {
    shutdown();
    default_biomes();
    { const char *on = getenv("MARE_GEOCHUNKS");
      if (on && atoi(on) == 0) { LOG_INFO("terrain", "terrain chunks off (MARE_GEOCHUNKS=0)"); return 0; } }
    std::vector<std::string> roots;
    { const char *e = getenv("MARE_HEIGHT_SCALE");
      if (e) { gc_height_scale_v = clampf((float)atof(e), 2.f, 60.f);
               LOG_INFO("terrain", "vertical exaggeration pinned: 9000 m -> %.1f world units (MARE_HEIGHT_SCALE)", gc_height_scale_v); } }
    probe(dir, roots);
    for (const std::string &r : roots) if ((int)ds.size() < GC_MAX_DATASETS) add_dataset(r.c_str());
    if (ds.empty()) { LOG_WARN("terrain", "no terrain dataset found (looked beside %s for data_chunks_terrain, data_new_2 and data_new): the procedural world stands alone", asset_root()); return 0; }
    // finest first: a regional build answers before the overview wherever both are resident
    std::stable_sort(ds.begin(), ds.end(), [](const gc_ds_t &a, const gc_ds_t &b) { return a.info.step_lat != b.info.step_lat ? a.info.step_lat < b.info.step_lat : a.area < b.area; });
    for (int i = 0; i < (int)ds.size(); ++i) ds[i].info.index = i;
    { const char *e = getenv("MARE_GEOCHUNK_MB"); if (e && atoi(e) > 0) budget = (size_t)atoi(e) * 1024 * 1024; }
    present = inited = 1;
    LOG_INFO("terrain", "%d terrain-v2 dataset%s, budget %zu MB, vertical exaggeration %.1fx", (int)ds.size(), ds.size() == 1 ? "" : "s", budget / (1024 * 1024), exaggeration());
    return (int)ds.size();
}

void GeoChunks::shutdown() {
    for (gc_ds_t &D : ds) {
        for (;;) { int busy = 0; for (int i = 0; i < D.nchunks; ++i) busy += D.chunk[i].state == GC_QUEUED;
                   if (!busy) break; jobs_help(); }
        for (int i = 0; i < D.nchunks; ++i) { gc_chunk_t *c = &D.chunk[i]; free(c->height); free(c->coverage); free(c->cls); free(c->splat); free(c->normal); }
        free(D.chunk);
    }
    ds.clear(); bytes = 0; present = inited = 0;
}

// ---------------------------------------------------------------------------
// load pipeline: IO read -> DECODE unpack -> MAIN publish

void GeoChunks::publish_cb(void *p) { geochunks().publish(p); }

void GeoChunks::publish(void *p) {           // MAIN
    gc_job_t *j = (gc_job_t *)p;
    gc_chunk_t *c = j->c;
    if (j->height && j->coverage) {
        c->height = j->height; c->coverage = j->coverage;
        c->cls = j->cls; c->splat = j->splat; c->normal = j->normal;
        size_t n2 = (size_t)ds[j->ds].info.samples * ds[j->ds].info.samples;
        c->bytes = n2 * (sizeof(float) + 1 + (c->cls ? 4 : 0) + (c->splat ? 4 : 0) + (c->normal ? 4 : 0));
        c->state = GC_READY;
        bytes += c->bytes; loaded_total++; last_read_ms = j->ms; gen++;
    } else {
        free(j->height); free(j->coverage); free(j->cls); free(j->splat); free(j->normal);
        c->state = GC_EMPTY; c->absent = 1; missing_files++;
    }
    free(j);
}

static unsigned char *gc_read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return nullptr;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = len > 0 ? (unsigned char *)malloc((size_t)len) : nullptr;
    size_t got = b ? fread(b, 1, (size_t)len, f) : 0;
    fclose(f);
    if (!b || got != (size_t)len) { free(b); return nullptr; }
    *n = got; return b;
}
static unsigned char *gc_read_exact(const char *path, size_t want) {   // a raw map, validated by length
    size_t n = 0; unsigned char *b = gc_read_file(path, &n);
    if (b && n != want) { LOG_WARN("terrain", "%s: %zu bytes, expected %zu - ignored", path, n, want); free(b); return nullptr; }
    return b;
}

void GeoChunks::decode_cb(void *p) {         // DECODE
    gc_job_t *j = (gc_job_t *)p;
    const int N = geochunks().ds[j->ds].info.samples;
    const size_t nsamp = (size_t)N * N;
    if (j->height) {
        // class map: biome id, coverage, provenance, coast - built here so the atlas uploads one RGBA8
        j->cls = (unsigned char *)malloc(nsamp * 4);
        if (j->cls) for (size_t i = 0; i < nsamp; ++i) {
            j->cls[i * 4 + 0] = j->biome ? j->biome[i] : 0;
            j->cls[i * 4 + 1] = j->coverage[i] ? 255 : 0;
            j->cls[i * 4 + 2] = j->source ? j->source[i] : (j->biome ? 2 : 0);
            j->cls[i * 4 + 3] = j->coast && j->coast[i] != 255 ? j->coast[i] : 0;
        }
        if (j->splat_png) {
            int w = 0, h = 0, n = 0;
            unsigned char *px = stbi_load_from_memory(j->splat_png, (int)j->splat_n, &w, &h, &n, 4);
            if (px && w == N && h == N) j->splat = px;
            else { if (px) LOG_WARN("terrain", "splat.png is %dx%d, expected %dx%d - ignored", w, h, N, N); stbi_image_free(px); }
        }
        if (j->normal_png) {
            int w = 0, h = 0, n = 0;
            unsigned char *px = stbi_load_from_memory(j->normal_png, (int)j->normal_n, &w, &h, &n, 4);
            if (px && w == N && h == N) j->normal = px;
            else { if (px) LOG_WARN("terrain", "normal.png is %dx%d, expected %dx%d - ignored", w, h, N, N); stbi_image_free(px); }
        }
    }
    free(j->biome); free(j->source); free(j->coast); free(j->splat_png); free(j->normal_png);
    j->biome = j->source = j->coast = j->splat_png = j->normal_png = nullptr;
    jobs_submit(JOB_LANE_MAIN, publish_cb, j, 0);
}

void GeoChunks::read_cb(void *p) {           // IO
    gc_job_t *j = (gc_job_t *)p;
    GeoChunks &g = geochunks();
    const gc_ds_t &D = g.ds[j->ds];
    char path[448], dir[400];
    double t0 = diag_now();
    const size_t nsamp = (size_t)D.info.samples * D.info.samples;
    // "chunks/{y}/{x}" (manifest chunk_directory; a package puts lods/<level>/ in front);
    // y = 0 is the northern row, x = 0 the west
    snprintf(dir, sizeof dir, "%s/%schunks/%d/%d", D.info.root, D.info.subdir, j->c->cy, j->c->cx);
    // the spec is explicit: validate byte lengths before using a map
    snprintf(path, sizeof path, "%s/height.r32", dir);
    j->height = (float *)gc_read_exact(path, nsamp * sizeof(float));
    if (j->height) {
        snprintf(path, sizeof path, "%s/coverage.r8", dir);     j->coverage = gc_read_exact(path, nsamp);
        // a chunk with heights but no coverage file is taken as fully covered: the mask is
        // there to find holes, and its absence is not a reason to drop real samples
        if (!j->coverage) { j->coverage = (unsigned char *)malloc(nsamp); if (j->coverage) memset(j->coverage, 1, nsamp); }
        snprintf(path, sizeof path, "%s/biome.r8", dir);        j->biome  = gc_read_exact(path, nsamp);
        snprintf(path, sizeof path, "%s/biome_source.r8", dir); j->source = gc_read_exact(path, nsamp);
        snprintf(path, sizeof path, "%s/coast.r8", dir);        { FILE *cf = fopen(path, "rb"); if (cf) { fclose(cf); j->coast = gc_read_exact(path, nsamp); } }
        snprintf(path, sizeof path, "%s/splat.png", dir);       j->splat_png  = gc_read_file(path, &j->splat_n);
        snprintf(path, sizeof path, "%s/normal.png", dir);      j->normal_png = gc_read_file(path, &j->normal_n);
    }
    j->ms = (diag_now() - t0) * 1000;
    jobs_submit(JOB_LANE_DECODE, decode_cb, j, 0);
}

void GeoChunks::request(int d, gc_chunk_t *c) {
    if (c->state != GC_EMPTY || c->absent) return;
    gc_job_t *j = (gc_job_t *)calloc(1, sizeof *j);
    j->c = c; j->ds = d;
    c->state = GC_QUEUED;
    jobs_submit(JOB_LANE_IO, read_cb, j, 0);
}

// ---------------------------------------------------------------------------
// the wanted set and the budget

// lon/lat -> chunk and the fractional sample position inside it. The global sample grid is
// chunks_x * GC_CELLS + 1 wide: neighbouring chunks share their edge samples, which is why the
// export's joins agree to 0 m and why fx can reach exactly GC_CELLS. A wrapping dataset takes
// any longitude; the others reject points outside their bbox, as the spec asks.
int GeoChunks::locate(const gc_ds_t &D, float lon, float lat, int *cx, int *cy, float *fx, float *fy) {
    const gc_dataset_t &I = D.info;
    if (I.wrap) { float span = I.east - I.west; lon = fmodf(lon - I.west, span); if (lon < 0) lon += span; lon += I.west; }
    else if (lon < I.west || lon > I.east) return 0;
    if (lat < I.south || lat > I.north) return 0;
    float u = (lon - I.west) / (I.east - I.west) * (float)(I.chunks_x * I.cells);
    float v = (I.north - lat) / (I.north - I.south) * (float)(I.chunks_y * I.cells);
    int X = (int)u, Y = (int)v;
    *cx = X / I.cells; *cy = Y / I.cells;
    if (*cx >= I.chunks_x) *cx = I.chunks_x - 1;
    if (*cy >= I.chunks_y) *cy = I.chunks_y - 1;
    *fx = u - (float)(*cx * I.cells);
    *fy = v - (float)(*cy * I.cells);
    return 1;
}

void GeoChunks::update(float lon, float lat, float radius_deg) {
    if (!inited) return;
    update_no++;
    int started = 0;
    for (int d = 0; d < (int)ds.size(); ++d) {
        gc_ds_t &D = ds[d]; const gc_dataset_t &I = D.info;
        float w = lon - radius_deg, e = lon + radius_deg, s = lat - radius_deg, n = lat + radius_deg;
        if (n < I.south || s > I.north) continue;                      // the view misses this dataset
        if (!I.wrap) { if (e < I.west || w > I.east) continue; w = clampf(w, I.west, I.east); e = clampf(e, I.west, I.east); }
        s = clampf(s, I.south, I.north); n = clampf(n, I.south, I.north);
        int x0, y0, x1, y1; float fx, fy;
        if (!locate(D, w, n, &x0, &y0, &fx, &fy) || !locate(D, e, s, &x1, &y1, &fx, &fy)) continue;
        int nx = I.chunks_x;
        if (I.wrap && x1 < x0) x1 += nx;        // the window crosses the antimeridian
        if (I.wrap && e - w >= I.east - I.west) { x0 = 0; x1 = nx - 1; }
        for (int y = y0; y <= y1; ++y) for (int xx = x0; xx <= x1; ++xx) {
            gc_chunk_t *c = at(d, I.wrap ? (xx % nx + nx) % nx : xx, y); if (!c) continue;
            c->last_want = update_no;
            if (c->state == GC_EMPTY && !c->absent && started < GC_MAX_INFLIGHT) { request(d, c); started++; }
        }
    }
    for (int guard = 0; guard < 8 && bytes > budget; ++guard) {
        gc_chunk_t *victim = nullptr;
        for (gc_ds_t &D : ds) for (int i = 0; i < D.nchunks; ++i) {
            gc_chunk_t *c = &D.chunk[i];
            if (c->state != GC_READY || c->last_want == update_no) continue;
            if (!victim || c->last_want < victim->last_want) victim = c;
        }
        if (!victim) break;
        free(victim->height); free(victim->coverage); free(victim->cls); free(victim->splat); free(victim->normal);
        victim->height = nullptr; victim->coverage = nullptr; victim->cls = victim->splat = victim->normal = nullptr;
        victim->state = GC_EMPTY;
        bytes -= victim->bytes; victim->bytes = 0; evicted++; gen++;
    }
}

// ---------------------------------------------------------------------------
// sampling

// the finest dataset with a resident, fully covered cell under the point
const gc_chunk_t *GeoChunks::find(float lon, float lat, int *dsi, float *fx, float *fy) {
    for (int d = 0; d < (int)ds.size(); ++d) {
        int cx, cy; float x, y;
        if (!locate(ds[d], lon, lat, &cx, &cy, &x, &y)) continue;
        const gc_chunk_t *c = at(d, cx, cy);
        if (!c || c->state != GC_READY) continue;
        const int N = ds[d].info.samples, C = ds[d].info.cells;
        int x0 = (int)x, y0 = (int)y;
        if (x0 > C - 1) x0 = C - 1;
        if (y0 > C - 1) y0 = C - 1;
        const unsigned char *cv = c->coverage;
        size_t i00 = (size_t)y0 * N + x0;
        // coverage.r8: 0 means the export had no input sample there. The spec is explicit that
        // this is not a land/sea mask - a hole is unknown ground, so the caller keeps what it
        // already had rather than being handed a fabricated height. A finer dataset with a hole
        // here hands over to the next one.
        if (!cv[i00] || !cv[i00 + 1] || !cv[i00 + N] || !cv[i00 + N + 1]) continue;
        *dsi = d; *fx = x; *fy = y;
        return c;
    }
    return nullptr;
}

// Smooth height reconstruction (0.22). The samples are ~3 km apart and the rings read them at
// ~1 km, so a bilinear read shows every sample as a crease - the faceted, sharp-edged ground
// the data's author asked us to smooth away. A Catmull-Rom spline over the 4 x 4 samples
// around the point passes through the samples (a peak stays a peak, the coastline does not
// move) and is C1 between them, so ridges round off and slopes bend instead of kinking. Where
// any of the sixteen taps is outside the chunk or marked missing the bilinear read stands in.
static inline float gc_cr(float a, float b, float c, float d, float t) {
    return b + 0.5f * t * (c - a + t * (2.f * a - 5.f * b + 4.f * c - d + t * (3.f * (b - c) + d - a)));
}
static float gc_height_at(const gc_chunk_t *c, int N, int C, float fx, float fy) {
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 > C - 1) x0 = C - 1;
    if (y0 > C - 1) y0 = C - 1;
    float tx = fx - x0, ty = fy - y0;
    const float *h = c->height; const unsigned char *cv = c->coverage;
    if (x0 >= 1 && y0 >= 1 && x0 + 2 <= C && y0 + 2 <= C) {
        int ok = 1; float rows[4];
        for (int j = -1; j <= 2 && ok; ++j) { const unsigned char *r = cv + (size_t)(y0 + j) * N + x0 - 1; ok = r[0] && r[1] && r[2] && r[3]; }
        if (ok) {
            for (int j = -1; j <= 2; ++j) { const float *r = h + (size_t)(y0 + j) * N + x0 - 1; rows[j + 1] = gc_cr(r[0], r[1], r[2], r[3], tx); }
            return gc_cr(rows[0], rows[1], rows[2], rows[3], ty);
        }
    }
    size_t i00 = (size_t)y0 * N + x0;
    return lerpf(lerpf(h[i00], h[i00 + 1], tx), lerpf(h[i00 + N], h[i00 + N + 1], tx), ty);
}

int GeoChunks::height_m(float lon, float lat, float *out) {
    int d; float fx, fy;
    const gc_chunk_t *c = find(lon, lat, &d, &fx, &fy);
    if (!c) return 0;
    *out = gc_height_at(c, ds[d].info.samples, ds[d].info.cells, fx, fy);
    return 1;
}

int GeoChunks::height_world(float lon, float lat, float *out) {
    float m;
    if (!height_m(lon, lat, &m)) return 0;
    *out = gc_world(m);
    return 1;
}

int GeoChunks::sample(float lon, float lat, gc_sample_t *out) {
    memset(out, 0, sizeof *out);
    out->normal[1] = 1.f;
    int d; float fx, fy;
    const gc_chunk_t *c = find(lon, lat, &d, &fx, &fy);
    if (!c) return 0;
    const int N = ds[d].info.samples, C = ds[d].info.cells;
    int x0 = (int)fx, y0 = (int)fy;
    if (x0 > C - 1) x0 = C - 1;
    if (y0 > C - 1) y0 = C - 1;
    float tx = fx - x0, ty = fy - y0;
    size_t i00 = (size_t)y0 * N + x0, i10 = i00 + 1, i01 = i00 + N, i11 = i01 + 1;
    out->dataset = d; out->has_height = 1;
    out->height_m = gc_height_at(c, N, C, fx, fy);
    out->height = gc_world(out->height_m);
    if (c->cls) { size_t near = (tx < 0.5f ? i00 : i10) + (ty < 0.5f ? 0 : N); out->biome = c->cls[near * 4]; out->coast = c->cls[near * 4 + 3]; }
    if (c->splat) {
        float w[4] = { 0, 0, 0, 0 };
        const float k[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
        const size_t idx[4] = { i00, i10, i01, i11 };
        for (int t = 0; t < 4; ++t) for (int ch = 0; ch < 4; ++ch) w[ch] += k[t] * c->splat[idx[t] * 4 + ch];
        float sum = w[0] + w[1] + w[2] + w[3];
        if (sum > 1.f) for (int ch = 0; ch < 4; ++ch) out->splat[ch] = w[ch] / sum;
        out->has_maps = 1;
    }
    if (c->normal) {
        float n[3] = { 0, 0, 0 };
        const float k[4] = { (1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty };
        const size_t idx[4] = { i00, i10, i01, i11 };
        for (int t = 0; t < 4; ++t) for (int ch = 0; ch < 3; ++ch) n[ch] += k[t] * (c->normal[idx[t] * 4 + ch] / 127.5f - 1.f);
        // east / up / south is the world's x / y / z; the slopes are true metres, the drawn
        // ground is exaggerated, so the horizontal components scale by the ratio
        float E = exaggeration();
        n[0] *= E; n[2] *= E;
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-6f) { out->normal[0] = n[0] / len; out->normal[1] = n[1] / len; out->normal[2] = n[2] / len; }
        out->has_maps = 1;
    }
    return 1;
}

int GeoChunks::resident(float lon, float lat) {
    int d; float fx, fy;
    return find(lon, lat, &d, &fx, &fy) != nullptr;
}

int GeoChunks::chunk_maps(int d, int cx, int cy, const unsigned char **cls, const unsigned char **splat, const unsigned char **normal) {
    const gc_chunk_t *c = at(d, cx, cy);
    if (!c || c->state != GC_READY) return 0;
    if (cls) *cls = c->cls; if (splat) *splat = c->splat; if (normal) *normal = c->normal;
    return 1;
}

void GeoChunks::stats(gc_stats_t *out) {
    memset(out, 0, sizeof *out);
    for (const gc_ds_t &D : ds) for (int i = 0; i < D.nchunks; ++i) {
        out->resident += D.chunk[i].state == GC_READY;
        out->loading  += D.chunk[i].state == GC_QUEUED;
        out->wanted   += D.chunk[i].last_want == update_no;
    }
    out->loaded_total = loaded_total; out->evicted = evicted; out->missing_files = missing_files;
    out->bytes = bytes; out->budget_bytes = budget; out->last_read_ms = last_read_ms;
    out->present = present; out->datasets = (int)ds.size();
    if (!ds.empty()) { out->chunks_x = ds[0].info.chunks_x; out->chunks_y = ds[0].info.chunks_y; }
}

// ---------------------------------------------------------------------------
// the one-shot geography fold (see terrain_chunks.h)

// Bilinear sample from a loaded row of chunks. `row[cx]` holds that chunk's heights and
// `cov[cx]` its coverage; both are NULL where the chunk is missing. Returns 0 (and leaves
// `out` alone) outside the row, on a missing chunk, or where coverage says the sample is not
// real - a hole is unknown ground, not sea.
static int gc_row_sample(float **row, unsigned char **cov, int chunks_x, int cyrow, int N, int C,
                         float u, float v, float *out) {
    int X = (int)u, Y = (int)v;
    int cx = X / C;
    if (cx < 0 || cx >= chunks_x || !row[cx]) return 0;
    if (Y / C != cyrow) return 0;
    int lx = X - cx * C, ly = Y - cyrow * C;
    if (lx > C - 1) lx = C - 1;
    if (ly > C - 1) ly = C - 1;
    float fx = u - (float)(cx * C + lx), fy = v - (float)(cyrow * C + ly);
    const float *h = row[cx]; const unsigned char *c = cov[cx];
    size_t i00 = (size_t)ly * N + lx, i10 = i00 + 1;
    size_t i01 = i00 + N, i11 = i01 + 1;
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
    const gc_dataset_t &I = b->d->info;
    const float bw = I.west, be = I.east, bs = I.south, bn = I.north;
    for (int r = begin; r < end; ++r) {
        int y = b->y0 + r;
        float lat = b->lat1 - (y + 0.5f) / b->cells_per_deg;
        for (int x = 0; x < b->w; ++x) {
            float lon = b->lon0 + (x + 0.5f) / b->cells_per_deg;
            if (lon < bw || lon > be || lat < bs || lat > bn) continue;
            int i = y * b->w + x, votes = 0, got = 0;
            float hc = 0, hmax = -1e9f;
            for (int q = 0; q < 9; ++q) {
                float u = (lon + off[q][0] / b->cells_per_deg - bw) / (be - bw) * b->gw;
                float v = (bn - (lat + off[q][1] / b->cells_per_deg)) / (bn - bs) * b->gh;
                float m;
                if (!gc_row_sample(b->row, b->cov, I.chunks_x, b->cy, I.samples, I.cells, u, v, &m)) continue;
                float hh = gc_world(m);
                votes += hh > 0.03f; got++;
                if (q == 0) hc = hh;
                if (hh > hmax) hmax = hh;
            }
            if (got < 9) continue;                           // a hole under part of the cell
            int was_land = b->lm[i] != 0, keep = b->protect && b->protect[i];
            float hmax_in = hmax;   // the highest land strictly inside the cell (centre taps + edges)
            if (was_land && hc <= 0.35f) {
                // the highest land anywhere under the cell (its edges and corners too), and - for a
                // land cell whose centre the data puts in the sea, i.e. mask land the raster had to
                // place a cell off the real strip - as far as the neighbouring cells' halves
                static const float edge[8][2] = { {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1} };
                const float reach[2] = { 0.48f, 0.85f }; int nreach = hc <= 0.03f ? 2 : 1;
                for (int rr = 0; rr < nreach; ++rr) for (int q = 0; q < 8; ++q) {
                    float u = (lon + edge[q][0] * reach[rr] / b->cells_per_deg - bw) / (be - bw) * b->gw;
                    float v = (bn - (lat + edge[q][1] * reach[rr] / b->cells_per_deg)) / (bn - bs) * b->gh;
                    float m;
                    if (gc_row_sample(b->row, b->cov, I.chunks_x, b->cy, I.samples, I.cells, u, v, &m)) { float hh = gc_world(m); hmax = maxf(hmax, hh); if (rr == 0) hmax_in = maxf(hmax_in, hh); }
                }
            }
            if (b->allow_flips && !was_land && votes >= 6 && !keep) {
                b->lm[i] = 1; b->hm[i] = maxf(hc, 0.45f); b->tally[k][1]++; b->tally[k][0]++;
            } else if (b->allow_flips && was_land && votes <= 1 && hc < -0.02f && hmax_in < 0.03f && !keep) {
                // the polygon says land, the data says sea under *all* of the cell - the nine
                // centre taps and the eight at its edges: water (a bay the hand-drawn coast
                // closed, a channel between islands drawn as one). The edge taps are what keep a
                // peninsula one sample wide (Peljesac at 2.8 km samples) from being eaten.
                b->lm[i] = 0; b->hm[i] = minf(hc, -0.12f); b->tally[k][1]++; b->tally[k][0]++;
            } else if (was_land && (votes >= 3 || (hc <= 0.03f && hmax > 0.03f))) {
                // A land cell whose centre the data puts in the sea or on the strand - a peninsula
                // or island a cell wide that slips between the cell centres (Peljesac: 5-7 km
                // against a 7 km cell) - takes the highest land under any of its nine taps, so a
                // thin coast keeps its hills instead of folding to a sandbar (0.22.3). A cell the
                // data already puts on high ground keeps its centre sample as before.
                float take = hc > 0.35f ? hc : maxf(hc, hmax);
                if (take > b->hm[i]) { b->hm[i] = take; b->tally[k][0]++; }
            } else if (!was_land && hc < 0.f && votes <= 1) {
                b->hm[i] = minf(hc, -0.12f); b->tally[k][0]++;
            }
        }
    }
}

int GeoChunks::bake(float *hm, unsigned char *lm, int w, int h, float lon0, float lat1,
                    float cells_per_deg, const unsigned char *protect, int *flips) {
    if (!present || ds.empty()) return 0;
    // the overview - the dataset with the widest bbox - folds the base world; regional builds
    // are presentation detail and never move the simulation's geography
    const gc_ds_t *D = &ds[0];
    for (const gc_ds_t &c : ds) if (c.area > D->area) D = &c;
    for (const gc_ds_t &c : ds) if (c.area == D->area && c.info.step_lat < D->info.step_lat) D = &c;   // among the widest, the finest level
    const gc_dataset_t &I = D->info;
    // The coastline follows the data (0.22.4): a cell the overview puts under water becomes
    // sea, one it puts on land becomes land - Istria, Peljesac and the island chains come out
    // of the GEBCO grid instead of hand-drawn polygons. Harbours are protected, the authored
    // straits are re-cut afterwards. MARE_TERRAIN_ISLANDS=0 keeps the polygons' mask.
    int allow_flips = 1;
    { const char *e = getenv("MARE_TERRAIN_ISLANDS"); if (e) allow_flips = atoi(e) != 0; }
    double t0 = diag_now();
    int changed = 0, newland = 0, missing = 0;
    const int C = I.cells;
    size_t nsamp = (size_t)I.samples * I.samples;
    float **row = (float **)calloc((size_t)I.chunks_x, sizeof *row);
    unsigned char **cov = (unsigned char **)calloc((size_t)I.chunks_x, sizeof *cov);
    if (!row || !cov) { free(row); free(cov); return 0; }
    float gw = (float)(I.chunks_x * C), gh = (float)(I.chunks_y * C);
    for (int cy = 0; cy < I.chunks_y; ++cy) {
        // does this chunk row touch the base grid at all? skip the whole read if not
        float rlat1 = I.north - (float)cy * (I.north - I.south) / I.chunks_y;
        float rlat0 = I.north - (float)(cy + 1) * (I.north - I.south) / I.chunks_y;
        float blat1 = lat1, blat0 = lat1 - h / cells_per_deg;
        if (rlat0 > blat1 || rlat1 < blat0) continue;
        for (int cx = 0; cx < I.chunks_x; ++cx) {
            // and this chunk column?
            float clon0 = I.west + (float)cx * (I.east - I.west) / I.chunks_x, clon1 = clon0 + (I.east - I.west) / I.chunks_x;
            if (clon1 < lon0 || clon0 > lon0 + w / cells_per_deg) continue;
            char path[448];
            snprintf(path, sizeof path, "%s/%schunks/%d/%d/height.r32", I.root, I.subdir, cy, cx);
            row[cx] = (float *)gc_read_exact(path, nsamp * sizeof(float));
            if (!row[cx]) { missing++; continue; }
            snprintf(path, sizeof path, "%s/%schunks/%d/%d/coverage.r8", I.root, I.subdir, cy, cx);
            cov[cx] = gc_read_exact(path, nsamp);
            if (!cov[cx]) { cov[cx] = (unsigned char *)malloc(nsamp); if (cov[cx]) memset(cov[cx], 1, nsamp); }
        }
        // Which base rows fall in this band, computed rather than scanned. The rows are then
        // folded in parallel - each writes its own cells, so the only shared state is the
        // counters, and those are per worker slot.
        int y0 = (int)ceilf((lat1 - rlat1) * cells_per_deg - 0.5f);
        int y1 = (int)floorf((lat1 - rlat0) * cells_per_deg - 0.5f);
        if (y0 < 0) y0 = 0;
        if (y1 > h - 1) y1 = h - 1;
        if (y1 >= y0) {
            bake_band_t bb = { D, row, cov, hm, lm, protect, w, h, cy, y0, lon0, lat1, cells_per_deg, gw, gh, allow_flips, {} };
            if (jobs_worker_count() > 0) jobs_parallel_for(JOB_LANE_WORKER, bake_rows, &bb, y1 - y0 + 1, 8);
            else bake_rows(&bb, 0, y1 - y0 + 1, 0);
            for (int k = 0; k < GC_BAKE_SLOTS; ++k) { changed += bb.tally[k][0]; newland += bb.tally[k][1]; }
        }
        for (int cx = 0; cx < I.chunks_x; ++cx) { free(row[cx]); row[cx] = NULL; free(cov[cx]); cov[cx] = NULL; }
    }
    free(row); free(cov);
    if (flips) *flips = newland;
    LOG_INFO("terrain", "geography baked from the terrain-v2 overview: %d cells written (%d new land), %d chunks absent, %.0f ms",
             changed, newland, missing, (diag_now() - t0) * 1000);
    return changed;
}

} // namespace mare::game::world

// C ABI (terrain_chunks.h) for the unported callers
extern "C" {
using mare::game::world::geochunks;
int  geochunks_init(const char *dir) { return geochunks().init(dir); }
void geochunks_shutdown(void) { geochunks().shutdown(); }
void geochunks_update(float lon, float lat, float radius_deg) { geochunks().update(lon, lat, radius_deg); }
int  geochunks_height_m(float lon, float lat, float *out) { return geochunks().height_m(lon, lat, out); }
int  geochunks_height(float lon, float lat, float *out) { return geochunks().height_world(lon, lat, out); }
int  geochunks_sample(float lon, float lat, gc_sample_t *out) { return geochunks().sample(lon, lat, out); }
int  geochunks_resident(float lon, float lat) { return geochunks().resident(lon, lat); }
void geochunks_stats(gc_stats_t *out) { geochunks().stats(out); }
unsigned geochunks_generation(void) { return geochunks().generation(); }
unsigned geochunks_update_number(void) { return geochunks().update_number(); }
int  geochunks_dataset_count(void) { return geochunks().dataset_count(); }
int  geochunks_dataset(int i, gc_dataset_t *out) { return geochunks().dataset(i, out); }
int  geochunks_chunk_maps(int ds, int cx, int cy, const unsigned char **cls, const unsigned char **splat, const unsigned char **normal) {
    return geochunks().chunk_maps(ds, cx, cy, cls, splat, normal); }
unsigned geochunks_chunk_wanted(int ds, int cx, int cy) { return geochunks().chunk_wanted(ds, cx, cy); }
const char *geochunks_biome_name(int id) { return geochunks().biome(id)->name; }
void geochunks_biome_rgb(int id, float rgb[3]) { const auto *b = geochunks().biome(id); for (int k = 0; k < 3; ++k) rgb[k] = b->rgb[k] / 255.f; }
float geochunks_exaggeration(void) { return geochunks().exaggeration(); }
int  geochunks_bake(float *hm, unsigned char *lm, int w, int h, float lon0, float lat1,
                    float cells_per_deg, const unsigned char *protect, int *flips) {
    return geochunks().bake(hm, lm, w, h, lon0, lat1, cells_per_deg, protect, flips);
}
}
