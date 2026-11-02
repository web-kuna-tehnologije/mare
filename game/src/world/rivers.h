// Rivers (docs/TODO.md item 5, 0.22): the drainage of the base heightfield, as polylines.
//
// Water runs downhill. A priority flood over the land cells of the base heightmap (every land
// cell is reached from the sea in order of height, so depressions fill and every cell has a
// downstream neighbour) gives a flow direction per cell; accumulating one unit of rain per
// cell down those directions gives the drainage area; cells draining more than RIVER_MIN_CELLS
// are river, traced from each head down to the sea or into the river they join. The points are
// smoothed and given a meander so a river is a curve, not a staircase of 7 km cells, and a
// half-width from its catchment. Deterministic: a pure function of the heightfield.
//
// Two consumers. The clipmap rings (gfx/terrain_rings.cc) carve the channel into the ground and
// write a river texture the terrain shader paints flowing water on. Navigation (world/nav.h)
// opens the lower reaches of the great rivers to ships: navriver marks the nav cells, by
// class, and nav_river_limit says how deep a fleet may go (fleet.cc sets it from the hulls).
#ifndef MARE_RIVERS_H
#define MARE_RIVERS_H

#ifdef __cplusplus
extern "C" {
#endif

#define RIVER_MIN_CELLS   45      // land-mask cells (49 km^2 each) a river needs to drain to exist
#define RIVER_NAV_CELLS  420      // ... to be navigable by anything (class 2: shallow draught)
#define RIVER_DEEP_CELLS 1400     // ... to take a merchantman (class 1: the great rivers' estuaries)
#define RIVER_MAX_POINTS 3000000   // 0.22.14: the package's segments (tools/rivers_pack.py at 15,000 km upstream) are ~1.3M points   // every river of the world after one Chaikin pass is ~360k; the cap only guards memory

typedef struct river_t { int first, count; int cells; int navigable; } river_t;   // points first..first+count-1, drainage at the mouth, nav class

// How wide a *navigable* river is drawn. A ship is drawn far out of scale here - a 25 m cog is
// 1.1 world units, where a world unit is ~6.9 km of real ground - so a river at its true width
// could never float one. A navigable reach is therefore cut at ship scale: wide enough that a
// hull fits inside its banks with water either side, which is what "sail up to Vienna" has to
// mean in a world where the ship is the thing you can see. Streams below the navigable classes
// keep their true hairline width.
#define RIVER_W_DEEP  1.05f       // half-width of a class-1 reach (a merchantman's estuary)
#define RIVER_W_NAV   0.60f       // ... of a class-2 reach (shallow draught)

// The same two classes on the *authored* rivers (0.22.18, todo 118), where the measure is the
// data's own `from_upstream_m` - the total length of watercourse above a segment - and not a
// catchment counted in 7 km cells. The thresholds are read off the data, by looking at what
// each threshold would open, over the whole layer and river by river (`assets/data-terrain-v2`;
// 10.9 Mm is the floor the export itself filtered at, so everything here is a major stem):
//
//   share of the layer's 1.65M points  |  greatest upstream length in the lower course
//     >= 15 Mm  74%    >=  90 Mm  9.4% |  Danube delta 338 · Rhine 110 · Elbe 94 · Po 59
//     >= 60 Mm  15%    >= 150 Mm  3.9% |  Ebro 50 · Seine 20 · Guadalquivir 17
//
// 15 Mm reaches Seville and Rouen but opens three quarters of every watercourse in Europe to
// shipping and cuts a ship-scale trench along each: not a river network, a canal system. 150
// and 60 keep it to the stems - the Danube and the lower Rhine take a merchantman, the Elbe,
// the upper Rhine and the Po's last reach take a shallow draught.
//
// What this measure cannot do: `from_upstream_m` is length, not discharge, so it cannot tell a
// large river in a small basin from a middling reach of a huge one. That is why the Seine and
// the Guadalquivir - both real river ports - fall short, and the layer has no width or flow
// figure to correct it with (`width_m` is null throughout). The Thames and the Tiber are not
// in the layer at all: they fall under the export's own filter, not under ours.
#define RIVER_UP_DEEP_M  150.e6f
#define RIVER_UP_NAV_M    60.e6f

// the polylines: world x, z, half-width (world units), flow direction (unit, world xz) per point
extern float *river_px, *river_pz, *river_hw, *river_dx, *river_dz;
extern unsigned char *river_cls;      // per point: 0 a stream (painted on the ground), 1 or 2 a navigable channel
extern unsigned char *river_mask;     // LM_W*LM_H: 1 where a river wide enough to need a bridge runs (roads.cc reads it)
extern int    river_npts, river_n;
extern river_t *rivers;

// Whether rivers may reshape the ground at all (0.22.3). Off by default: the rings paint a
// river on the surface at its true hairline width and nothing is cut, no navigable channel is
// opened and no fleet enters a river. MARE_RIVER_CARVE=1 brings the 0.22.1 channels back
// (ship-scale beds below sea level, navriver). The authored river data to come replaces this.
int  rivers_carve_enabled(void);
// build everything from the base heightmap and land mask (after the geography bake, before nav_init)
void rivers_generate(void);
void rivers_shutdown(void);

// A spatial grid of river segments over a rectangle, for the ring composer: built once per
// compose on the main thread, queried from the workers (read only).
typedef struct river_grid_t { float x0, z0, cell; int nx, nz; int *first, *count, *idx; int nidx; } river_grid_t;
void  rivers_grid_build(river_grid_t *g, float x0, float z0, float extent, float cell);
void  rivers_grid_free(river_grid_t *g);
// distance to the nearest river centreline near (wx, wz); returns 0 when none is within 3 cells.
// `hw`, `dx`, `dz` get the half-width and flow direction interpolated along that segment.
int   rivers_grid_query(const river_grid_t *g, float wx, float wz, float *dist, float *hw, float *dx, float *dz, int *cls);

#ifdef __cplusplus
}
#endif

#endif
