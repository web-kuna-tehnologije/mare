// Navigation.
//
// A coarser water grid (NAV_DIV land-mask cells per nav cell; a cell is water if at least half
// of its sub-cells are) is searched with A* (octile heuristic, coast penalty, no corner cutting).
// Paths are string-pulled with line-of-sight tested on the fine land mask so segments never cut
// a coast, and waypoints snap to a water sub-cell. Water bodies are labelled at init so requests
// between different bodies (lakes, the Black Sea basin) are rejected in O(1). Port-to-port
// distances are cached per source port (one Dijkstra row each, computed on first use; rows are
// never estimated, which keeps the simulation deterministic across save/load).

#ifndef MARE_NAV_H
#define MARE_NAV_H

#include "../core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char *navgrid;    // NAV_W*NAV_H, 1 = navigable water
extern unsigned char *navcoast;   // 1 = water cell touching land (path cost penalty)

static inline int   nav_x(float wx)  { return (int)((wx / WORLD_K - LON0) * CELLS_PER_DEG / NAV_DIV); }
static inline int   nav_y(float wz)  { return (int)((LAT1 + wz / WORLD_K) * CELLS_PER_DEG / NAV_DIV); }
static inline float nav_wx(int nx)   { return (LON0 + (nx + 0.5f) * NAV_DIV / CELLS_PER_DEG) * WORLD_K; }
static inline float nav_wz(int ny)   { return -(LAT1 - (ny + 0.5f) * NAV_DIV / CELLS_PER_DEG) * WORLD_K; }
static inline int   nav_ok(int nx, int ny) { return nx >= 0 && ny >= 0 && nx < NAV_W && ny < NAV_H && navgrid[ny*NAV_W+nx]; }

void  nav_init(void);                                    // build the grid and water-body labels (after world_init)
int   nav_nearest(float wx, float wz, int *nx, int *ny, int maxr);   // nearest water cell within maxr cells
int   nav_snap(float *wx, float *wz, int maxr);          // move a point onto the nearest water (fine-grid precise)
int   nav_snap_reachable(float *wx, float *wz, int maxr, float from_x, float from_z); // ...restricted to the water body of (from)
// A* path between world points. Returns length in world units or <0 if unreachable.
// px/pz/npts may be NULL to only query reachability/length
float nav_path(float sx, float sz, float ex, float ez, float *px, float *pz, int *npts, int maxpts);

// port distances
void  nav_ports_reset(void);                             // forget all rows (new game / load)
float nav_port_dist(int a, int b);                       // sea distance in world units, <0 unreachable (computes row a on first use)
void  nav_ports_precompute(void);                        // optional loading step: all rows now
void  nav_ports_added(int port_index);                   // after appending a settlement: one row + column, no full reset

#ifdef __cplusplus
}
#endif

#endif
