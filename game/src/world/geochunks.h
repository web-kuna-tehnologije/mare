// The terrain export read as the chunks it is (assets/data_new/MARE_INTEGRATION.md, format
// "mare-terrain-v2"; assets/data_new_2/world-game-package/HOW_TO_USE_DATA.md, format
// "mare-world-package-v1" - the same chunks with LOD levels and a few more maps).
//
// A dataset is a directory with a manifest.json and chunks/{y}/{x}/ (a package: one per
// lods/{level}/) holding, per chunk, N x N vertex-aligned samples - 513 for the full-resolution
// chunks, 257 / 129 / 65 for the package's LOD levels and the regional Dalmatian export -
// neighbouring chunks sharing their edge samples:
//
//   height.r32        little-endian float32 metres, negative is sea floor   (authoritative)
//   coverage.r8       1 = an input sample existed, 0 = unknown ground (NOT a land/sea mask)
//   biome.r8          land-cover class id (0 missing, 1 ocean, 10 trees ... 100 moss/lichen)
//   biome_source.r8   0 missing height, 1 WorldCover, 2 heuristic
//   splat.png         RGBA8 linear: grass / rock / sand / snow weights (sum 255 on land, 0 on water)
//   normal.png        RGB8 linear, east / up / south basis, true-metre slopes
//   texture.png       RGB8 sRGB palette (overview albedo; not read - the game paints its own)
//   coast.r8          (package) 0 not coast, 1 sand beach, 2 rock cliff, 3 rocky coast, 4 coastal water, 255 missing
//   depth.r32 / tessellation.r8   (package) derived from height; not read
//
// Several datasets can be present at once - the 3 km world overview and any finer regional
// builds the exporter cut - and every sampler below answers from the **finest resident** one,
// so dropping a 30 m Dalmatian build under assets/data_new sharpens that coast and changes
// nothing else. Independent builds are not stitched (their grids need not align); the rings
// that consume them cross-fade by distance anyway (gfx/streamview.cc).
//
// Streaming follows the house discipline (world/stream.h): the IO lane reads, the DECODE lane
// unpacks the PNGs, the MAIN lane publishes, so a chunk's state only ever changes on the render
// thread and nothing here needs a lock. GL-free, so headless tests can drive it.
//
// What this is not: finer data. The overview is ~3 km per sample, "za globalni pregled".
// Resampling it does not add detail; a finer regional build does.

#ifndef MARE_GEOCHUNKS_H
#define MARE_GEOCHUNKS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GC_SAMPLES      513           // the full-resolution chunk's samples per edge (the GPU atlas slot size)
#define GC_CELLS        512           // ... hence cells; a dataset's own N is gc_dataset_t.samples
#define GC_MAX_DATASETS 16            // the package's LOD levels plus regional builds
#define GC_MAX_CHUNKS   4096          // per dataset (the overview has 27 x 14 = 378)
#define GC_BUDGET_MB    160           // resident payload over all datasets; MARE_GEOCHUNK_MB overrides
#define GC_MAX_INFLIGHT 4             // chunk reads started per update (~2.4 MB on disk each)

// land-cover classes, as the manifest lists them
enum {
    GC_BIOME_MISSING = 0, GC_BIOME_OCEAN = 1, GC_BIOME_TREES = 10, GC_BIOME_SHRUBS = 20,
    GC_BIOME_GRASS = 30, GC_BIOME_CROPS = 40, GC_BIOME_BUILT = 50, GC_BIOME_BARE = 60,
    GC_BIOME_SNOW = 70, GC_BIOME_WATER = 80, GC_BIOME_WETLAND = 90, GC_BIOME_MANGROVE = 95,
    GC_BIOME_MOSS = 100
};

typedef struct gc_stats_t {
    int    resident, wanted, loading;
    int    loaded_total, evicted, missing_files;
    size_t bytes, budget_bytes;
    double last_read_ms;
    int    present;                   // at least one dataset parsed
    int    datasets;
    int    chunks_x, chunks_y;        // of the first (finest) dataset
} gc_stats_t;

typedef struct gc_dataset_t {
    int   index, chunks_x, chunks_y, wrap;
    int   samples, cells;             // N and C of this dataset's chunks
    int   lod;                        // the package's level (0 = full), 0 for a plain v2 export
    float west, south, east, north;   // bbox, degrees
    float step_lon, step_lat;         // degrees per sample (manifest pixel_step_degrees)
    char  root[320];                  // the dataset directory
    char  subdir[32];                 // "" or "lods/<level>": chunks live at root/subdir/chunks/{y}/{x}
} gc_dataset_t;

// everything the chunks know about one geographic point
typedef struct gc_sample_t {
    int   has_height, has_maps;       // height answered (all four taps covered) / the maps did
    int   dataset;                    // which dataset answered
    float height_m, height;           // metres; world units (the game's calibration, below)
    int   biome;                      // class id at the nearest sample
    int   coast;                      // package coast class at the nearest sample (0 where the dataset has none)
    float splat[4];                   // grass, rock, sand, snow - normalised, all 0 on water/missing
    float normal[3];                  // world axes (x east, y up, z south), slopes exaggerated to world scale
} gc_sample_t;

// Find the datasets and read their manifests (no payload). `dir` NULL = probe
// "<assets>/../data_new", "<assets>/data_new", then "assets/data_new" upwards; a dir is either
// one dataset (it has a manifest.json) or a folder of them. MARE_GEOCHUNK_DIR overrides,
// MARE_GEOCHUNKS=0 disables. Returns the number of datasets accepted.
int  geochunks_init(const char *dir);
void geochunks_shutdown(void);

// Once per frame: page in the chunks under a camera looking at (lon, lat) with a view radius
// of `radius_deg`, in every dataset that covers it, and drop the coldest once over budget.
void geochunks_update(float lon, float lat, float radius_deg);

// Height in *metres* at a geographic point, bilinear over the samples of the finest resident
// dataset. Returns 0 when no covering chunk is resident or a tap is marked missing in
// coverage.r8, in which case `out` is untouched and the caller keeps whatever it had.
int  geochunks_height_m(float lon, float lat, float *out_metres);

// The same, converted to world units with the calibration the rest of the game is tuned for:
// land linear to 9000 m, sea sqrt-compressed to 12000 m (world/geochunks.cc GC_*).
int  geochunks_height(float lon, float lat, float *out_world);

// Everything at once (height, class, weights, normal). Returns 1 when the height answered;
// `has_maps` says whether the material maps did too.
int  geochunks_sample(float lon, float lat, gc_sample_t *out);

// One-shot geography fold straight from the overview dataset (world/terrainmap.h drives it):
// walk the chunk grid one row of chunks at a time - the whole export is never resident - and
// write the authored Earth into a base grid of `w` x `h` cells. Per cell: authored relief
// where both agree on land, authored bathymetry where both agree on sea, base land never
// removed, new land only under MARE_TERRAIN_ISLANDS. `protect` (optional, w*h bytes, 1 =
// keep) pins the harbour basins. Returns cells written, 0 when no dataset is present.
int  geochunks_bake(float *hm, unsigned char *lm, int w, int h,
                    float lon0, float lat1, float cells_per_deg,
                    const unsigned char *protect, int *flips);

int  geochunks_resident(float lon, float lat);
void geochunks_stats(gc_stats_t *out);
unsigned geochunks_generation(void);      // bumps when residency changes

// the datasets, finest first
int  geochunks_dataset_count(void);
int  geochunks_dataset(int i, gc_dataset_t *out);

// The decoded maps of a resident chunk, for the GPU chunk atlas (gfx/chunkatlas.h). Each is
// samples^2 RGBA8, northern row first:
//   cls    R = biome id, G = coverage (0/255), B = biome_source, A = coast class (0 none)
//   splat  the export's weights as shipped
//   normal the export's RGB with A = 255
// Returns 1 when resident (pointers valid until the next geochunks_update), 0 otherwise.
int  geochunks_chunk_maps(int ds, int cx, int cy, const unsigned char **cls,
                          const unsigned char **splat, const unsigned char **normal);
unsigned geochunks_chunk_wanted(int ds, int cx, int cy);   // update number it was last wanted in (0 = never)
unsigned geochunks_update_number(void);

// the manifest's class table (defaults from the spec when a manifest lacks it)
const char *geochunks_biome_name(int id);
void        geochunks_biome_rgb(int id, float rgb[3]);
// horizontal-to-vertical scale ratio of the world: a true-metre slope times this is the
// slope of the drawn ground (the shaders apply it to the export's normals)
float       geochunks_exaggeration(void);

#ifdef __cplusplus
}
#endif

#endif
