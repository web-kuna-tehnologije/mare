// The GEBCO export, read as the chunks it actually is (assets/data/how_to.txt).
//
// `assets/data/export-global` is the whole Earth as 27 x 14 chunks of 513 x 513 little-endian
// float32 samples in *metres* - negative is sea floor - plus a coverage byte per sample (1 =
// a real input sample, 0 = missing; it is NOT a land/sea mask) and a tile.json with the
// chunk's WGS84 bounds. The how_to is explicit about two things the game was not doing:
//
//   "Za igru koristi height.r32"  - use the float32, not the 16-bit preview, and
//   "coverage.r8 koristi za prepoznavanje nedostajućih uzoraka"
//
// Until now the game read a *resample* of this export: tools/terrain_from_gebco.py cut it to
// 40 samples per degree and quantised it to u16 (world/terraintiles.h). That round trip costs
// the exact values, the coverage information and the exported edge agreement - the how_to
// notes the joins were verified at 0 m, and this loader keeps that property by construction
// because it reads the published samples rather than a re-gridded copy of them.
//
// What this is not: finer data. The export is ~3 km per sample, a global overview, as the
// how_to says plainly. The fine regional set (height_fine, ~520 m over the Mediterranean,
// gfx/heightatlas.h) is what carries the near ground; this carries the whole world exactly.
//
// Streaming follows the same discipline as the rest (world/stream.h, world/terraintiles.h):
// the IO lane reads, the MAIN lane publishes, so a chunk's state only ever changes on the
// render thread and nothing here needs a lock. GL-free, so tests/stream_test.cpp can drive it.

#ifndef MARE_GEOCHUNKS_H
#define MARE_GEOCHUNKS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC_SAMPLES     513            // samples per chunk edge (manifest: samples_per_chunk)
#define GC_CELLS       512            // ... hence cells (manifest: cells_per_chunk)
#define GC_MAX_CHUNKS  512            // the export has 27 x 14 = 378
#define GC_BUDGET_MB   96             // resident payload; MARE_GEOCHUNK_MB overrides
#define GC_MAX_INFLIGHT 4             // chunk reads started per update (1.3 MB each)

typedef struct gc_stats_t {
    int    resident, wanted, loading;
    int    loaded_total, evicted, missing_files;
    size_t bytes, budget_bytes;
    double last_read_ms;
    int    present;                   // the export was found and the manifest parsed
    int    chunks_x, chunks_y;
} gc_stats_t;

// Find the export and read its manifest (no payload). `dir` NULL = probe
// "<assets>/../data/export-global" then "<assets>/data/export-global" then
// "assets/data/export-global" upwards. MARE_GEOCHUNK_DIR overrides, MARE_GEOCHUNKS=0 disables.
// Returns 1 when the manifest parsed and the chunk grid is usable.
int  geochunks_init(const char *dir);
void geochunks_shutdown(void);

// Once per frame: page in the chunks under a camera looking at (lon, lat) with a view radius
// of `radius_deg`, and drop the coldest ones once over budget.
void geochunks_update(float lon, float lat, float radius_deg);

// Height in *metres* at a geographic point, bilinear over the 513x513 samples. Returns 0 when
// the covering chunk is not resident or any of the four samples is marked missing in
// coverage.r8, in which case `out` is untouched and the caller keeps whatever it had.
int  geochunks_height_m(float lon, float lat, float *out_metres);

// The same, converted to world units with the calibration the rest of the game is tuned for
// (world/terraintiles.h TT_*): land linear to 9000 m, sea sqrt-compressed to 12000 m. This is
// exactly what the u16 cut decodes to, so every shading threshold, wave gate and depth fade
// keeps its meaning - what changes is that the value is exact instead of quantised.
int  geochunks_height(float lon, float lat, float *out_world);

int  geochunks_resident(float lon, float lat);
void geochunks_stats(gc_stats_t *out);
unsigned geochunks_generation(void);      // bumps when residency changes

#ifdef __cplusplus
}
#endif

#endif
