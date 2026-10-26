// Mesh helpers on top of the RHI (rhi.h) used by every render pass.
//
// * mb_t: a flat-shaded triangle-mesh builder (12 floats per vertex: position, normal, colour,
//   material, roughness, metalness). Material 1 vertices take the per-instance colour (nation
//   stripes, flags, roofs). Roughness/metalness are builder *state*: set them with
//   mb_material() and every vertex emitted afterwards carries them (PBR, DESIGN_RENDERER.md).
// * mesh_t / inst_t: instanced drawing of a mesh; instance data is streamed every frame.

#ifndef MARE_GPU_H
#define MARE_GPU_H

#include "rhi.h"

#define UNI(p, name) rhi_uniform(&(p), name)

// mesh builder. Double-sided surfaces (mb_quad2) emit a single quad: the mesh shader flips the
// flat normal towards the viewer, so a mirrored copy would only add exactly coplanar triangles
// that z-fight their twins.
#define MB_FLOATS 12                 // floats per vertex: pos3 nrm3 col3 mat1 rough1 metal1
typedef struct mb_t { float *v; int n, cap; float rough, metal; float nrm[3], nrm_blend; } mb_t;
// PBR surface presets (roughness, metalness) for the procedural meshes
#define MB_WOOD      0.78f, 0.f
#define MB_PAINT     0.55f, 0.f
#define MB_CLOTH     0.90f, 0.f
#define MB_STONE     0.85f, 0.f
#define MB_TILE      0.70f, 0.f
#define MB_METAL     0.35f, 1.f
static inline void mb_material(mb_t *m, float rough, float metal) { m->rough = rough; m->metal = metal; }
// Normal override, also builder state: blends the per-triangle flat normal towards `n`. Used for
// surfaces that are modelled faceted but should read as smooth - a billowed sail is a fan of
// triangles whose flat normals split it into hard light and dark wedges.
static inline void mb_set_normal(mb_t *m, float x, float y, float z, float blend) { m->nrm[0]=x; m->nrm[1]=y; m->nrm[2]=z; m->nrm_blend = blend; }
static inline void mb_auto_normal(mb_t *m) { m->nrm_blend = 0; }
void mb_tri (mb_t *m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float r, float g, float b, float mat);  // CCW front face
void mb_quad(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat);   // two triangles
void mb_quad2(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat);  // double sided
void mb_box (mb_t *m, float cx, float cy, float cz, float hx, float hy, float hz, float r, float g, float b, float mat);                // axis aligned, half extents

// instancing. inst_t = position + heading (radians, 0 = +x), uniform scale + rgb, misc:
//   sway (roll animation 0/1), sel (highlight 0..1), bob (vertical bobbing 0/1), xlen (>0: stretch along x, for walls/piers)
//   dither: LOD cross-fade (gfx/lod.h). 0 = opaque; +t keeps a (1-t) share of the pixels, -t the
//   complementary t share, through one ordered 4x4 pattern, so the two LODs of a fading instance
//   partition the screen exactly (no holes, no double coverage, no sorting). fx1..3 are reserved.
typedef struct inst_t { float x, y, z, heading; float scale, r, g, b; float sway, sel, bob, xlen; float dither, fx1, fx2, fx3; } inst_t;
typedef struct mesh_t { rhi_geometry_t geo; int ninst; } mesh_t;
extern const rhi_layout_t mesh_layout;                // builder vertices + inst_t instances (meshes.c program)
mesh_t mesh_upload(mb_t *m);                          // uploads and frees the builder
mesh_t mesh_upload_indexed(const float *verts, int nverts, const unsigned *idx, int nidx);   // builder-format vertices + u32 indices (authored assets)
// Instances are streamed to the GPU once per frame (mesh_set_instances, during scene_build_*)
// and then drawn by every pass that needs them (shadow cascades, main) without re-uploading.
void   mesh_set_instances(mesh_t *m, const inst_t *inst, int n);
void   mesh_draw(mesh_t *m);                          // draws the instances set this frame; needs a bound pipeline

// growable CPU-side instance list
typedef struct instlist_t { inst_t *v; int n, cap; } instlist_t;
inst_t *instlist_push(instlist_t *l);
static inline void instlist_clear(instlist_t *l) { l->n = 0; }

#endif
