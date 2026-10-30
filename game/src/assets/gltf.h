// glTF 2.0 / GLB loader (CPU side, no GL): parses a .glb or .gltf file into flat vertex
// streams with every node transform baked in, plus per-primitive material ranges. The asset
// runtime (asset.c) turns this into builder-format vertices and uploads via gpu.h; this file
// never touches the renderer so headless tests can exercise it.
//
// Supported: GLB container, embedded JSON .gltf, base64 data: URIs, external .bin buffers,
// POSITION/NORMAL/TEXCOORD_0/COLOR_0 attributes, u8/u16/u32 indices, node TRS or matrix
// hierarchies, pbrMetallicRoughness factors. Not supported (rejected with a clear error or
// ignored): sparse accessors, Draco/meshopt compression, skins, animations, non-triangle
// primitive modes, samplers (images are handed over as encoded bytes; asset.c bakes the base
// colour texture into vertex colours since the game's meshes are untextured).

#ifndef MARE_GLTF_H
#define MARE_GLTF_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gltf_material_t {
    char  name[64];
    float base_color[4];           // baseColorFactor (sRGB, straight alpha)
    float rough, metal;            // roughnessFactor, metallicFactor
    int   double_sided;
    int   base_tex;                // index into images (baseColorTexture), -1 none
} gltf_material_t;

// encoded image bytes (PNG/JPEG as stored in the file); decoding is the caller's business
typedef struct gltf_image_t {
    unsigned char *data; size_t size;
    char mime[32];                 // "image/png", "image/jpeg" or "" when unknown
    char name[64];
} gltf_image_t;

// one draw range: indices [first_index, first_index+nindices) referencing the shared vertex
// streams, shaded with materials[material] (-1 = glTF default material)
typedef struct gltf_prim_t {
    int material;
    int first_index, nindices;
    char mesh_name[64];            // the glTF mesh this range came from
    char node_name[64];            // the node that placed it (LOD selection keys on this: Church_LOD1)
} gltf_prim_t;

typedef struct gltf_model_t {
    // vertex streams, nverts each, world space of the default scene (node transforms baked).
    // nrm/col/uv are always allocated: missing normals are generated flat per triangle,
    // missing colours default to white, missing UVs to 0.
    float    *pos;                 // xyz
    float    *nrm;                 // xyz, unit
    float    *col;                 // rgb (COLOR_0 * baseColorFactor is NOT pre-multiplied here)
    float    *uv;                  // xy
    int       nverts;
    unsigned *idx; int nidx;       // triangle list
    gltf_prim_t     *prims; int nprims;
    gltf_material_t *mats;  int nmats;
    gltf_image_t    *images; int nimages;
    float bmin[3], bmax[3];        // bounds of the baked positions
    char  error[256];              // set when a load returns 0
} gltf_model_t;

// Load from a file (.glb or .gltf; external buffers resolve relative to the file) or from
// memory (`dir` resolves external buffers, NULL forbids them). Return 1 on success; on failure
// return 0 with m->error describing the first problem. The model is zeroed first either way.
int  gltf_load_file(const char *path, gltf_model_t *m);
int  gltf_load_memory(const void *data, size_t size, const char *dir, gltf_model_t *m);
void gltf_free(gltf_model_t *m);

#ifdef __cplusplus
}
#endif

#endif
