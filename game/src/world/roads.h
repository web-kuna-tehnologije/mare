// Roads (docs/TODO.md item 6, 0.22.2): the tracks that tie a hinterland to its ports.
//
// Every inland settlement (sim/region.h, G.inland) is joined to the hub port of its region
// with the most influence - the biggest town, the fort counting - by a least-effort route
// over the base heightfield: an A* on the land-mask grid whose step cost grows with the slope
// crossed, forbids water, and is cheaper along cells that already carry a road, so tracks
// from neighbouring farms run together into one road to town instead of fanning out. The
// hubs of a region are chained to each other the same way.
//
// A road's class follows the trade it carries: a region moving little through its links has
// tracks, one near its link capacity has a highway. Classes are recomputed every few
// simulated weeks from region_t.traded_today / throughput, so a road "upgrades" as the inland
// trade towards the port grows. Where a route crosses a river a bridge point is recorded for
// the prop pass to come.
//
// The polylines are consumed by the clipmap rings (gfx/terrain_rings.cc), which paint them
// into the ring's road channel; terrain.frag draws packed earth there. Deterministic: a
// function of the heightfield and the settlement set, no rng.
#ifndef MARE_ROADS_H
#define MARE_ROADS_H

#ifdef __cplusplus
extern "C" {
#endif

#define ROAD_MAX_POINTS  400000
#define ROAD_MAX_BRIDGES 4096
#define ROAD_REBUILD_DAYS 45       // trade volumes move the classes; rebuild this often

typedef struct road_t { int first, count; int cls; int from_inland, to_port; } road_t;   // cls 0 track, 1 road, 2 highway
typedef struct bridge_t { float x, z, dx, dz; float hw; } bridge_t;       // where a road crosses a river: position, road direction, river half-width

extern float *road_px, *road_pz, *road_hw;
extern unsigned char *road_cls;
extern int    road_npts, road_n;
extern road_t *roads;
extern bridge_t *road_bridges; extern int road_nbridges;

// Build (or rebuild) the network from the current ports, inland settlements and regions.
// Needs the world and the game state; cheap enough to call when the seed changes and every
// ROAD_REBUILD_DAYS. Returns the number of roads.
int  roads_generate(void);
void roads_shutdown(void);
double roads_last_build_day(void);     // G.day of the last build (-1e9 = never)

typedef struct road_grid_t { float x0, z0, cell; int nx, nz; int *first, *count, *idx; int nidx; } road_grid_t;
void roads_grid_build(road_grid_t *g, float x0, float z0, float extent, float cell);
void roads_grid_free(road_grid_t *g);
// nearest road centreline near (wx, wz): distance, half-width, class; 0 when none is near
int  roads_grid_query(const road_grid_t *g, float wx, float wz, float *dist, float *hw, int *cls);

#ifdef __cplusplus
}
#endif

#endif
