// mare::engine::render::ChunkAtlas - the terrain-v2 chunks' material maps, on the GPU.
//
// The clipmap rings (gfx/terrain_rings.cc) carry a chunk's *height*; its other maps - the biome
// class per sample, the grass/rock/sand/snow weights and the terrain-scale normal - are what
// the ground is painted with, and they are sampled per fragment, so they live on the GPU as
// they are: resident chunks are paged into slots of three RGBA8 atlases (class / splat /
// normal, terrain_chunks.h for the channel layout) and a tiny slot table maps a chunk index to its
// slot. terrain/chunks.glsl addresses them with the spec's vertex-aligned texel rule
// ((sample + 0.5) / slot texels inside the slot), nearest for the class map, bilinear for the
// rest, no mipmaps (independent per-chunk mips seam; the base level is what the spec asks for).
//
// One dataset is bound at a time - the world-covering one, finest first, so the 4x build
// (2049 samples a chunk) displaces the 3 km overview here as it does in the rings. Regional
// builds still only sharpen the rings' heights; giving them slots too is the next step
// (docs/TODO.md).
#ifndef MARE_CHUNKATLAS_H
#define MARE_CHUNKATLAS_H

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

// The slot grid is chosen at init from the dataset's own sample count, because a slot's cost
// is quadratic in it: 4x4 slots of 513 is 50 MB of GPU, but 4x4 of 2049 would be 800 MB. The
// atlas therefore trades slots for texels and never spends more than ~115 MB:
//
//   dataset samples   slot texels   grid   GPU (three RGBA8 atlases)   degrees covered
//   513 (3 km)        513           4 x 4   50 MB                      52 x 50
//   2049 (4x, 700 m)  1025          3 x 3  113 MB                      39 x 38
//
// A dataset finer than the slot is point-sampled down by an integer stride on upload (the
// grids are 2^k + 1, so the stride divides exactly and the shared edge samples still line up,
// which is what keeps chunk joins seamless). MARE_CHUNK_ATLAS_SAMPLES pins the slot size -
// 2049 for the full material detail of the 4x build at 2 x 2 slots and 200 MB.
#define CA_COLS 4           // slots across, at most ...
#define CA_ROWS 4           // ... and down; chunk_atlas picks the grid it can afford
#define CA_SLOTS (CA_COLS * CA_ROWS)
#define CA_SLOT_BUDGET_TEXELS 3075   // the widest atlas edge the grid may reach (3 x 1025)
#define CA_UPLOADS_PER_FRAME 1   // a slot is three uploads (1 MB at 513, 4 MB at 1025); one chunk a frame hides it

// once per frame, after geochunks_update: slots for the wanted resident chunks, uploads budgeted
void chunkatlas_update(void);
// bind the three atlases, the slot table and the geometry uniforms on a pipeline that includes
// terrain/chunks.glsl
void chunkatlas_bind(const rhi_pipeline_t *p, int unit_cls, int unit_splat, int unit_normal, int unit_slots);
int  chunkatlas_resident(void);      // slots in use (dashboard / metrics)
int  chunkatlas_available(void);     // the atlas exists and holds at least one chunk

// the uniform names terrain/chunks.glsl declares; append to a pipeline's uniform list
#define GLSL_CHUNK_UNIFORMS "u_ccls", "u_csplat", "u_cnrm", "u_cslots", "u_cgeo", "u_cgrid", "u_cslot", "u_con"

#ifdef __cplusplus
}
#endif

#endif
