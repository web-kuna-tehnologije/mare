// Asset runtime: the standard way to describe, load and cache authored 3D assets.
//
// An asset is a directory with a JSON descriptor and one or more glTF/GLB files. The descriptor
// (`<name>.asset.json`) says what the asset is, how big it should be in world units, where its
// pivot goes, which way it faces, how its materials map onto the game's PBR vertex format, and
// which LODs exist. The registry reads a manifest listing every descriptor; assets load on first
// use (asset_get) and stay cached until asset_unload_all.
//
// Everything here is CPU-side and GL-free: the result of a load is one builder-format vertex
// array (gpu.h MB_FLOATS layout: pos, normal, colour, material slot, roughness, metalness) plus
// a u32 index list per LOD, already normalised to world scale and pivot. The renderer uploads
// those lazily (gfx/scene.c) and frees the CPU copy.
//
// Descriptor example (game/assets/church/church.asset.json):
//   {
//     "name": "church", "type": "building",
//     "file": "church.glb",                       // default file for every LOD
//     "lods": [ { "node": "Church_LOD0", "dist": 0 },        // LODs by node name in one file ...
//               { "node": "Church_LOD1", "dist": 40 },
//               { "file": "church_far.glb", "dist": 120 } ], // ... or by separate file
//     "scale":  { "fit": "height", "size": 0.45 },           // or { "factor": 0.01 }
//     "pivot":  "base_center",                                // keep | center | base_center
//     "pivot_offset": [0, 0, 0],                              // world units, after normalisation
//     "forward": "+z",                                        // model axis that should face heading 0 (+x)
//     "materials": [ { "match": "roof", "rough": 0.7, "metal": 0, "color": [0.6,0.3,0.2],
//                      "instance_color": false, "bake_texture": true } ],
//     "collision": { "type": "aabb" }                         // none | aabb | sphere (parsed, unused yet)
//   }

#ifndef MARE_ASSET_H
#define MARE_ASSET_H

#include <stddef.h>

#define ASSET_MAX          256
#define ASSET_MAX_LODS     4
#define ASSET_MAX_MATRULES 8
#define ASSET_PATH         256

typedef enum asset_type_t  { ASSET_PROP, ASSET_BUILDING, ASSET_SHIP, ASSET_SHIP_PART, ASSET_TERRAIN, ASSET_TYPE_COUNT } asset_type_t;
typedef enum asset_pivot_t { PIVOT_KEEP, PIVOT_CENTER, PIVOT_BASE_CENTER } asset_pivot_t;
typedef enum asset_fit_t   { FIT_NONE, FIT_HEIGHT, FIT_LENGTH, FIT_WIDTH, FIT_MAX } asset_fit_t;   // which extent "size" refers to (y, x, z, largest)
typedef enum asset_coll_t  { COLL_NONE, COLL_AABB, COLL_SPHERE } asset_coll_t;

// material rule: applies to every glTF material whose name contains `match` (case-insensitive;
// "*" matches all). Unset fields keep the glTF values.
typedef struct asset_matrule_t {
    char  match[48];
    float rough, metal, color[3];
    int   has_rough, has_metal, has_color;
    int   instance_color;     // material slot 1: vertices take the per-instance colour (nation flag, roof)
    int   bake_texture;       // sample baseColorTexture into vertex colours (default 1)
} asset_matrule_t;

typedef struct asset_lod_desc_t { char file[ASSET_PATH]; char node[64]; float dist; } asset_lod_desc_t;

typedef struct asset_desc_t {
    char name[64];
    char dir[ASSET_PATH];                 // directory the descriptor lives in (files resolve against it)
    asset_type_t type;
    asset_lod_desc_t lod[ASSET_MAX_LODS]; int nlods;
    asset_fit_t fit; float fit_size;      // fit != FIT_NONE: scale so that extent == fit_size
    float scale_factor;                   // used when fit == FIT_NONE (default 1)
    asset_pivot_t pivot; float pivot_offset[3];
    int forward;                          // 0 +x, 1 +z, 2 -x, 3 -z (model forward axis)
    asset_matrule_t mat[ASSET_MAX_MATRULES]; int nmat;
    asset_coll_t coll; float coll_radius, coll_half[3];   // from "collision" (radius/half optional: derived from bounds)
} asset_desc_t;

typedef struct asset_lod_t {
    float    *verts; int nverts;          // MB_FLOATS (12) floats per vertex, world scale, pivot applied
    unsigned *idx;   int nidx;
    float     dist;                       // use from this camera distance on
    int       gpu;                        // renderer-owned flag: 1 once uploaded (verts/idx may then be freed)
} asset_lod_t;

typedef enum asset_state_t { ASSET_UNLOADED, ASSET_LOADED, ASSET_FAILED } asset_state_t;

typedef struct asset_t {
    asset_desc_t  desc;
    asset_state_t state;
    asset_lod_t   lod[ASSET_MAX_LODS]; int nlods;
    float scale;                          // final model->world factor
    float bmin[3], bmax[3], radius;       // normalised bounds (LOD 0) and bounding-sphere radius about the pivot
    float coll_radius, coll_half[3];      // collision shape, derived when the descriptor leaves them 0
    size_t cpu_bytes;                     // vertex + index memory held on the CPU
    size_t gpu_bytes;                     // what the renderer uploaded (asset_lod_t.gpu set by scene.c)
    double load_ms;
    char  error[256];
} asset_t;

// registry
int      asset_registry_load(const char *manifest_path);   // registers every descriptor listed; returns count (0 + log on error)
int      asset_register_desc(const char *desc_path);       // one descriptor file; returns index or -1
int      asset_count(void);
asset_t *asset_at(int index);
asset_t *asset_find(const char *name);                     // registered asset, loaded or not
asset_t *asset_get(const char *name);                      // find + load on first use; NULL if unknown or failed
int      asset_load(asset_t *a);                           // 1 ok; 0 with a->error (idempotent)
void     asset_unload(asset_t *a);                         // frees CPU data, back to ASSET_UNLOADED
void     asset_unload_all(void);
void     asset_free_cpu(asset_t *a, int lod);              // renderer: drop the CPU copy after upload
size_t   asset_cpu_bytes(void);                            // total held by the cache (CPU side)
size_t   asset_gpu_bytes(void);                            // total uploaded by the renderer
const char *asset_root(void);                              // resolved assets directory (MARE_ASSETS or game/assets up the tree)

// LOD for a camera distance (world units): the last LOD whose dist * asset_lod_bias <= d.
// The bias is a global quality knob (dashboard): 2 holds detailed LODs twice as far,
// 0.5 drops to coarse LODs at half the authored distance.
int          asset_pick_lod(const asset_t *a, float d);
extern float asset_lod_bias;

// building blocks, exposed for tests and tools
int  asset_desc_parse(const char *json, size_t n, const char *dir, asset_desc_t *d, char *err, size_t errcap);
int  asset_desc_validate(const asset_desc_t *d, char *err, size_t errcap);
int  asset_build(asset_t *a);                              // loads files named in a->desc and fills the LODs
const char *asset_type_name(asset_type_t t);

#endif
