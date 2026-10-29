// World streaming (game/docs/DESIGN_STREAMING.md): the tile hierarchy
// continent -> region -> coastal tile -> tactical chunk, streamed from disk under a memory
// budget with an LRU unloading policy.
//
// The world extent is covered by four levels of tiles, each a 2x2 split of the one above
// (a quadtree whose leaves are the 64x64-cell tactical chunks the renderer already culls by).
// Every tile carries a STREAM_RES x STREAM_RES *delta* heightfield against the base heightmap,
// so a missing tile is simply "no extra detail", never a hole. Deltas are deterministic fbm
// octaves beyond the base map's resolution, faded out below STREAM_FADE_H and over water so
// coasts, harbours and town ground never move.
//
// Loading goes through the job system with the same discipline as asset.c: disk reads and
// writes on the IO lane, generation on the DECODE lane (low priority), the state flip on the
// MAIN lane - tile state only ever changes on the thread that called stream_init. The
// simulation never reads tiles (world_height stays pure); deltas are presentation only.
//
// This file is GL-free so tests/stream_test.c can drive it headlessly.

#ifndef MARE_STREAM_H
#define MARE_STREAM_H

#include "../core/types.h"

#define STREAM_LEVELS   4
#define STREAM_RES      257                   // payload samples per tile edge (257x257 floats)
#define STREAM_L0_CELLS 512                   // landmask cells per L0 tile edge; halves per level
#define STREAM_FADE_H   0.5f                  // base height below which the delta fades to 0
#define STREAM_SPLIT_K  2.0f                  // a child tile is wanted while the camera is within K x the child's size of its centre
#define STREAM_MAX_INFLIGHT 8                 // tile loads started per stream_update at most
#define STREAM_EVICT_PER_UPDATE 4

enum { STREAM_L_CONTINENT = 0, STREAM_L_REGION, STREAM_L_COASTAL, STREAM_L_TACTICAL };

typedef enum stream_state_t { TILE_EMPTY = 0, TILE_QUEUED, TILE_READY } stream_state_t;

typedef struct stream_tile_t {
    unsigned char level, state;
    unsigned char water_only;                 // no land in the tile: never loaded, never drawn
    unsigned char on_disk;                    // a cache file exists (eviction keeps it)
    unsigned char authored;                   // an authored tile_l<level>_<tx>_<ty> asset exists (set by the renderer)
    unsigned char wanted;                     // in this update's wanted set
    short tx, ty;                             // tile coordinates within the level's grid
    float *delta;                             // STREAM_RES*STREAM_RES when READY, else NULL
    unsigned last_want;                       // update counter the tile was last wanted (LRU)
} stream_tile_t;

typedef struct stream_stats_t {
    int resident, wanted, loading, water;     // tiles
    int evicted, disk_hits, generated;        // cumulative since stream_init
    size_t bytes, budget_bytes;               // resident payload memory vs the budget
    double last_gen_ms, last_io_ms;           // cost of the last generation / disk read
} stream_stats_t;

// lifecycle. Call from the thread that owns the main loop, after world_init. `tile_dir` is the
// disk cache directory (created if missing); NULL = "tiles". MARE_STREAM_MB overrides the budget.
void stream_init(unsigned seed, const char *tile_dir);
void stream_shutdown(void);                   // waits for loads in flight, frees every payload

// Once per frame (or per test step): compute the wanted set for a camera at look-at (cx, cz)
// with distance `dist`, queue loads for missing wanted tiles, evict over budget (LRU, never a
// wanted tile, never a tile whose children are resident).
void stream_update(float cx, float cz, float dist);

// grid shape and access
int                  stream_grid_w(int level);
int                  stream_grid_h(int level);
int                  stream_tile_count(void);                 // all levels
stream_tile_t       *stream_tile(int level, int tx, int ty);  // NULL outside the grid
stream_tile_t       *stream_tile_at_index(int i);             // 0 .. stream_tile_count()-1
void                 stream_tile_rect(const stream_tile_t *t, float *x0, float *z0, float *x1, float *z1);
const char          *stream_l1_name(int tx, int ty);          // geographic name of an L1 region tile
void                 stream_tile_label(const stream_tile_t *t, char *buf, int n);   // "L3 12,7 (Caribbean & Sargasso)"

// finest resident delta under a world position (bilinear); 0 when nothing is resident there.
float    stream_delta_at(float wx, float wz);
// the same, but ignoring levels finer than `max_level`: the coarse clipmap ring asks for L2 so
// it never tries to carry L3 detail its texel size cannot sample without aliasing.
float    stream_delta_at_level(float wx, float wz, int max_level);
unsigned stream_residency_generation(void);   // bumps whenever a tile becomes READY or is evicted
void     stream_stats(stream_stats_t *out);
size_t   stream_budget_bytes(void);
void     stream_set_budget(size_t bytes);     // dashboard / tests; MARE_STREAM_MB sets it at init

// The renderer marks tiles that have an authored mesh (asset `tile_l<level>_<tx>_<ty>`), so the
// debug overlay can list the grids still to model. stream.c itself never touches the registry.
void stream_set_authored(int level, int tx, int ty, int authored);

// Write a top-down shaded-relief reference image of one tile (binary PPM, `res` square) and
// report its placement. This is the image a chunk is modelled from - see docs/BLENDER_LOD.md
// section 4 and `mare --map-chunk=level,tx,ty`. Returns 0 (and logs) if the tile is out of
// range or the file cannot be written.
typedef struct stream_chunk_ref_t {
    float lon0, lat0, lon1, lat1;     // geographic extent (north-west to south-east)
    float x0, z0, x1, z1;             // world rect
    float size_units, size_deg;       // tile edge
    float min_h, max_h, land_frac;    // terrain under it
} stream_chunk_ref_t;
int stream_dump_chunk_reference(int level, int tx, int ty, int res, const char *path,
                                stream_chunk_ref_t *out);

#endif
