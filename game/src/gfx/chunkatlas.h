// mare::engine::render::ChunkAtlas - the terrain-v2 chunks' material maps, on the GPU.
//
// The clipmap rings (gfx/streamview.cc) carry a chunk's *height*; its other maps - the biome
// class per sample, the grass/rock/sand/snow weights and the terrain-scale normal - are what
// the ground is painted with, and they are sampled per fragment, so they live on the GPU as
// they are: resident chunks are paged into slots of three RGBA8 atlases (class / splat /
// normal, geochunks.h for the channel layout) and a tiny slot table maps a chunk index to its
// slot. terrain/chunks.glsl addresses them with the spec's vertex-aligned texel rule
// ((sample + 0.5) / 513 inside the slot), nearest for the class map, bilinear for the rest,
// no mipmaps (independent per-chunk mips seam; the base level is what the spec asks for first).
//
// One dataset is bound at a time - the overview, which covers the whole world. Regional
// builds already sharpen the rings' heights; giving their materials slots too is the next
// step (docs/TODO.md).
#ifndef MARE_CHUNKATLAS_H
#define MARE_CHUNKATLAS_H

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CA_COLS 4           // slots across ...
#define CA_ROWS 4           // ... and down: 16 chunks (13 degrees each) resident on the GPU
#define CA_UPLOADS_PER_FRAME 1   // a slot is three 1 MB uploads; one chunk a frame hides it

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
