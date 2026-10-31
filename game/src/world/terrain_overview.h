// Authored terrain maps (game/docs/DESIGN_TERRAIN.md): the real Earth, as three images.
//
// The procedural heightfield in world.c was always a placeholder - a coast ramp plus fbm. This
// replaces its *detail* with authored data, as a set of equirectangular images covering the
// world extent:
//
//   assets/terrain/depth_map_terrain.png    height and bathymetry, grey 0..255
//   assets/terrain/normal_map_terrain.png   matching surface normals, tangent space RGB
//   assets/terrain/terrain_textures.png     a TERRAIN_ATLAS_X x TERRAIN_ATLAS_Y atlas of
//                                           ground materials (rock, grass, soil, sand, ...)
//
// None of them is required: a missing map leaves the procedural world exactly as it was, so
// the game runs with the art half-finished and improves as each file lands.
//
// Why chunks of quads. The terrain renderer already draws the world as 64x64-cell chunks
// (gfx/terrain.c) picked by distance. The authored maps slot into that unchanged: a chunk
// samples the depth map instead of the procedural field, the normal map instead of a finite
// difference, and chooses an atlas cell per fragment from height, slope and latitude. What the
// streamer adds (world/stream.h) is residency - which chunks have their slice of the maps
// resident at what resolution - so the whole Earth never has to be in memory at once.
//
// The maps are loaded once on the IO lane, kept as 8-bit CPU images (a 1376x768 pair is ~3 MB)
// and uploaded whole; per-chunk streaming of *slices* becomes worthwhile only once the source
// images are far larger than this, and the sampling API below is written so that change does
// not reach the renderer.

#ifndef MARE_TERRAINMAP_H
#define MARE_TERRAINMAP_H

#include "../core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The atlas layout in terrain_textures.png: cells are read left to right, top to bottom.
#define TERRAIN_ATLAS_X 6
#define TERRAIN_ATLAS_Y 3

// Ground materials, in the order the atlas holds them. The index is what the terrain shader
// looks up, so re-ordering the atlas means re-ordering this.
enum {
    GROUND_ROCK = 0, GROUND_GRASS, GROUND_SOIL, GROUND_SAND, GROUND_SHINGLE, GROUND_CRACKED,
    GROUND_DUNE, GROUND_MEADOW, GROUND_MOSSY_STONE, GROUND_GRAVEL, GROUND_SAND2, GROUND_PEAT,
    GROUND_CLAY, GROUND_TURF, GROUND_MOSS, GROUND_COBBLE, GROUND_PEBBLE, GROUND_SAND3,
    GROUND_COUNT
};

typedef struct terrainmap_t {
    unsigned char *depth;  int dw, dh, dc;    // height / bathymetry
    unsigned char *normal; int nw, nh, nc;    // surface normals
    unsigned char *atlas;  int aw, ah, ac;    // ground material atlas
    float lon0, lon1, lat0, lat1;             // the extent the maps cover (degrees)
    float sea_level;                          // the grey value that is sea level, 0..1
    float height_scale;                       // world units between sea level and pure white
    int   loaded;                             // 1 once at least the depth map is in
} terrainmap_t;

extern terrainmap_t TM;

// Load whatever is present under `dir` (NULL = "<assets>/terrain"). Safe to call with nothing
// there: `TM.loaded` stays 0 and every sampler below falls through to the procedural world.
// Blocking, ~30 ms for the current maps; call once at startup after world_init.
void  terrainmap_load(const char *dir);
void  terrainmap_free(void);

// Height in world units at a geographic position, sea level 0, from the depth map. Returns
// `fallback` (pass the procedural world_height) wherever no map covers the point.
float terrainmap_height(float lon, float lat, float fallback);

// Surface normal at a geographic position; writes a unit vector in world axes. Returns 0 and
// leaves `out` alone when there is no normal map.
int   terrainmap_normal(float lon, float lat, float out[3]);

// Which atlas cell belongs at a point: the material a fragment of ground should be painted
// with, from height, slope and latitude. Pure function of its arguments, so the same rule can
// run on the CPU (placement, minimap) and in the shader.
int   terrainmap_ground(float height, float slope, float lat);

// Blend the authored height field into the base heightmap that world.c already built, so the
// simulation, navigation and the renderer all see one consistent world. No-op when no depth
// map is loaded. Call after terrainmap_load.
void  terrainmap_apply_to_world(void);
// Fold the authored *geography* in from the streamed tile set (world/terraintiles.h): islands
// the procedural world lacks become land, shared land takes the authored relief, shared sea the
// authored bathymetry, and existing land is never removed. Harbour basins are pinned, the
// carved straits re-opened and the coast-distance field rebuilt, so navigation and port
// placement stay sound. Call between terraintiles_init and nav_init; MARE_TERRAIN_GEO=0
// disables it. Returns the number of cells changed.
int   terrainmap_bake_geography(const char *tiles_dir);

#ifdef __cplusplus
}
#endif

#endif
