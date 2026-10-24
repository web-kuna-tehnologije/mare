// Mesh helpers on top of the RHI (rhi.h) used by every render pass.
//
// * mb_t: a flat-shaded triangle-mesh builder (10 floats per vertex: position, normal, colour,
//   material). Material 1 vertices take the per-instance colour (nation stripes, flags, roofs).
// * mesh_t / inst_t: instanced drawing of a mesh; instance data is streamed every frame.

#ifndef MARE_GPU_H
#define MARE_GPU_H

#include "rhi.h"

#define UNI(p, name) rhi_uniform(&(p), name)

// mesh builder
typedef struct mb_t { float *v; int n, cap; } mb_t;
void mb_tri (mb_t *m, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz, float r, float g, float b, float mat);  // CCW front face
void mb_quad(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat);   // two triangles
void mb_quad2(mb_t *m, const float a[3], const float b[3], const float c[3], const float d[3], float r, float g, float b_, float mat);  // double sided
void mb_box (mb_t *m, float cx, float cy, float cz, float hx, float hy, float hz, float r, float g, float b, float mat);                // axis aligned, half extents

// instancing. inst_t = position + heading (radians, 0 = +x), uniform scale + rgb, misc:
//   sway (roll animation 0/1), sel (highlight 0..1), bob (vertical bobbing 0/1), xlen (>0: stretch along x, for walls/piers)
typedef struct inst_t { float x, y, z, heading; float scale, r, g, b; float sway, sel, bob, xlen; } inst_t;
typedef struct mesh_t { rhi_geometry_t geo; } mesh_t;
extern const rhi_layout_t mesh_layout;                // builder vertices + inst_t instances (meshes.c program)
mesh_t mesh_upload(mb_t *m);                          // uploads and frees the builder
void   mesh_draw(mesh_t *m, const inst_t *inst, int n);   // requires meshes_bind() first (render.h)

// growable CPU-side instance list
typedef struct instlist_t { inst_t *v; int n, cap; } instlist_t;
inst_t *instlist_push(instlist_t *l);
static inline void instlist_clear(instlist_t *l) { l->n = 0; }

#endif
