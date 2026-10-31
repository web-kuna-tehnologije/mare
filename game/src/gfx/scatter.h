// Static props of the hinterland (0.22.3, docs/TODO.md 7b): the orchard and olive trees, the
// vine bushes and the sheep that stand on the land the rings' land-use masks paint. Low-poly
// procedural meshes, instanced, placed deterministically from the near ring's texture when it
// recomposes, drawn in the main pass only - they cast no shadow and never enter the shadow or
// the staged scenes. MARE_SCATTER=0 turns them off.
#ifndef MARE_SCATTER_H
#define MARE_SCATTER_H

#ifdef __cplusplus
extern "C" {
#endif

// Rebuild the instance lists from the near ring (terrain_rings.cc sv_ring_compose): the ring's
// RGBA8 land-use texels (fields, vineyards, orchards, villages), its height delta, resolution,
// origin and texel size. Main thread, GL context current.
// `sets` is nsets x 4 floats: world x, z, built radius, kind (0 farm village, 1 mine, 2 fur
// post, 3 port town) - the mills, the mine headframes and the houses stand on them.
void scatter_rebuild(const unsigned char *landuse, const unsigned char *forest, const float *delta, int res, float x0, float z0, float step,
                     const float *sets, int nsets);
extern float scatter_density;        // forest density knob (MARE_FOREST_DENSITY, 1 = normal)
int  scatter_submit(void);           // after meshes_bind(); returns instances drawn
extern int scatter_enabled;
extern int scatter_count;            // instances in the current build (dashboard)

#ifdef __cplusplus
}
#endif

#endif
