// Static world geometry.
//
// The coastline polygons in data/geo_coast.h are rasterized (even-odd) into a land mask of
// CELLS_PER_DEG cells per degree; lakes are carved out and straits forced open. From the mask a
// signed distance-to-coast field drives a procedural heightfield (positive land, negative sea
// floor) that the renderer samples as a texture and the simulation uses for placement. The
// territory map (one byte per TERR_DIV cells: nation+1) colours the map view.

#ifndef MARE_WORLD_H
#define MARE_WORLD_H

#include "../core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned char *landmask;   // LM_W*LM_H, 1 = land (row 0 = north)
extern short         *coastdist;  // cells to the nearest coast: +land, -water (chessboard BFS; the simulation's placement rules read this)
extern float         *coastdist_f; // the same as an exact *Euclidean* distance in cells (0.22): +land, -water, +-0.5 on the
                                  // cells either side of the line so it crosses zero exactly on the coastline. Shapes the
                                  // base shelf and plain (compute_heightmap) and the shore profile (gfx/streamview.cc):
                                  // chessboard isolines are rounded squares, which is what made islands boxes
extern float         *heightmap;  // LM_W*LM_H world units, sea level 0
extern unsigned char *territory;  // TERR_W*TERR_H: 0 none, else owning nation + 1
extern int            territory_dirty; // 1 = recompute from ports, 2 = recomputed, upload to the GPU

void  world_init(unsigned seed);             // allocate + rasterize + heightfield (~0.5 s); aborts if LM_W/LM_H mismatch the extents
// After a geography fold (world/terrainmap.h) changed the land mask: force the authored straits
// open again (Gibraltar and friends must never silt up, whatever the fold decided) and rebuild
// the signed distance-to-coast field so town placement keeps working near the new coasts.
void  world_recarve_straits(void);
void  world_recompute_coastdist(void);
float world_height(float wx, float wz);      // bilinear height at a world position
int   world_is_land(float wx, float wz);     // land mask lookup (outside the map counts as land)
void  world_compute_territory(void);         // nation of the nearest settled port within its influence radius

// value noise / fbm shared with procedural placement (deterministic for a seed)
float world_fbm(float x, float y, int octaves, unsigned seed);
float world_vnoise(float x, float y, unsigned seed);

#ifdef __cplusplus
}
#endif

#endif
