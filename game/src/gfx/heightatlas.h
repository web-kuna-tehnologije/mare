// mare::engine::render::HeightAtlas - the resident authored terrain, on the GPU, at its own
// resolution.
//
// The clipmap rings (gfx/streamview.cc) compose every height source into one 512-texel window
// per ring. That window is the terrain's only authored input, and at the near ring's 80 world
// units it is 0.156 units - about 0.04 degrees - per texel. The fine regional tile set is
// ~520 m, which is 0.0056 degrees: the ring carries one sample in seven of what the data
// actually has, and the rest is thrown away before the vertex stage ever sees it. That is why
// the near coast has looked like a smooth ramp while the tiles underneath it hold the real
// shape of the shore.
//
// This keeps the resident fine tiles as a GPU texture atlas and lets the terrain's vertex and
// tessellation stages sample them directly. What is added is only the *detail the ring cannot
// carry*: the shader reconstructs what the ring holds (the same field point-sampled on the
// ring grid and interpolated) and subtracts it, so the two layers sum to the data exactly once
// - the atlas cannot double count, and it falls to zero wherever the ring already resolves the
// ground or no tile is resident. Nothing here can move the shoreline the ports and the
// navigation grid were placed against: the residual is zero-mean detail, not a new coastline.
//
// A slot is a whole tile (TT_TILE square) converted to world units once on upload, so the
// shader reads a plain height and needs no decode.

#ifndef MARE_HEIGHTATLAS_H
#define MARE_HEIGHTATLAS_H

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HA_COLS 4          // slots across the atlas ...
#define HA_ROWS 4          // ... and down: 16 fine tiles resident on the GPU at once
#define HA_UPLOADS_PER_FRAME 1    // a slot is a 1 MB upload plus its decode; one a frame hides it

// Once per frame, after terraintiles_update: give slots to the fine tiles that are resident on
// the CPU and near the camera, release the ones that are not.
void heightatlas_update(void);
// Bind the atlas, its slot lookup and the geometry uniforms on a pipeline that includes
// terrain/atlas.glsl. `ring_texel` is the world size of one clipmap-ring texel - what the
// shader subtracts as "already carried" - or 0 to make the atlas contribute nothing.
void heightatlas_bind(const rhi_pipeline_t *p, int unit_atlas, int unit_slots, float ring_texel);
int  heightatlas_resident(void);     // slots in use (dashboard / metrics)
int  heightatlas_available(void);    // the atlas exists and holds at least one tile

// The uniform names terrain/atlas.glsl declares; append to a pipeline's uniform list.
#define GLSL_ATLAS_UNIFORMS "u_hatlas", "u_hslots", "u_ha_geo", "u_ha_grid", "u_ha_slot", "u_ha_on", "u_ha_k"

#ifdef __cplusplus
}
#endif

#endif
