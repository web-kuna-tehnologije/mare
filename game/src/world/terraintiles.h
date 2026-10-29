// Streamed terrain tiles (game/docs/DESIGN_TERRAIN.md): the authored Earth, paged in.
//
// The source maps are a 32768x32768 16-bit heightfield (2.1 GB decoded) and its normal map
// (3.2 GB). Neither can be held, so tools/terrain_tiles.py cuts the playable window into
// 512x512 tiles of raw samples - 384 of each, 480 MB on disk - and this pages them in around
// the camera and drops the ones it has stopped looking at.
//
// A tile is one read of a fixed-size file with no header and no decode, which is what lets a
// load sit on the IO lane and finish inside a frame. The same discipline as the rest of the
// streamer (world/stream.h): the IO lane reads, the MAIN lane publishes, so a tile's state
// only ever changes on the render thread and nothing here needs a lock.
//
// Nothing is required. With no tiles on disk every sampler falls through to the procedural
// world and the game is exactly what it was.

#ifndef MARE_TERRAINTILES_H
#define MARE_TERRAINTILES_H

#include <stddef.h>

#define TT_TILE        512          // samples per tile edge (must match tools/terrain_tiles.py)
#define TT_MAX_TILES   2048
#define TT_BUDGET_MB   192          // resident tile payload; MARE_TERRAIN_MB overrides
#define TT_MAX_INFLIGHT 6           // tile reads started per update

typedef enum tt_state_t { TT_EMPTY = 0, TT_QUEUED, TT_READY } tt_state_t;

typedef struct tt_tile_t {
    unsigned char state, kind;      // kind 0 = height (u16), 1 = normal (rgb8)
    short         tx, ty;
    void         *data;             // TT_TILE^2 samples when READY
    unsigned      last_want;
} tt_tile_t;

typedef struct tt_set_t {           // one kind of map
    char   dir[256], kind[16];
    int    tiles_x, tiles_y, bytes_per_sample;
    float  lon0, lat0, lon1, lat1;  // the geographic window the tiles cover
    int    width, height;           // total samples
    int    present;                 // a <kind>.json was found and parsed
    tt_tile_t *tile; int ntiles;
} tt_set_t;

typedef struct tt_stats_t {
    int resident, wanted, loading;
    int loaded_total, evicted;
    size_t bytes, budget_bytes;
    double last_read_ms;
} tt_stats_t;

// Read <dir>/height.json and <dir>/normal.json and build the tile grids (no payload yet).
// `dir` NULL = "<assets>/../terrain/tiles". Returns 1 if at least the height set is present.
int  terraintiles_init(const char *dir);
void terraintiles_shutdown(void);

// Once per frame: page in the tiles under a camera looking at (lon, lat) with a view radius of
// `radius_deg` degrees, and drop the coldest ones once over budget.
void terraintiles_update(float lon, float lat, float radius_deg);

// Height in world units (sea level 0) and surface normal at a geographic point. Both return 0
// when the covering tile is not resident, so callers keep whatever they already had.
int  terraintiles_height(float lon, float lat, float *out);
int  terraintiles_normal(float lon, float lat, float out[3]);

// Is the tile covering this point resident? (The renderer asks before it decides to ask for a
// finer surface.)
int  terraintiles_resident(float lon, float lat);

void terraintiles_stats(tt_stats_t *out);
unsigned terraintiles_generation(void);   // bumps when residency changes

#endif
