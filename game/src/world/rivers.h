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
// Two consumers. The clipmap rings (gfx/streamview.cc) carve the channel into the ground and
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
#define RIVER_MAX_POINTS 1200000   // every river of the world after one Chaikin pass is ~360k; the cap only guards memory

typedef struct river_t { int first, count; int cells; int navigable; } river_t;   // points first..first+count-1, drainage at the mouth, nav class

// How wide a *navigable* river is drawn. A ship is drawn far out of scale here - a 25 m cog is
// 1.1 world units, where a world unit is ~6.9 km of real ground - so a river at its true width
// could never float one. A navigable reach is therefore cut at ship scale: wide enough that a
// hull fits inside its banks with water either side, which is what "sail up to Vienna" has to
// mean in a world where the ship is the thing you can see. Streams below the navigable classes
// keep their true hairline width.
#define RIVER_W_DEEP  1.05f       // half-width of a class-1 reach (a merchantman's estuary)
#define RIVER_W_NAV   0.60f       // ... of a class-2 reach (shallow draught)

// the polylines: world x, z, half-width (world units), flow direction (unit, world xz) per point
extern float *river_px, *river_pz, *river_hw, *river_dx, *river_dz;
extern unsigned char *river_cls;      // per point: 0 a stream (painted on the ground), 1 or 2 a navigable channel
extern int    river_npts, river_n;
extern river_t *rivers;

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
