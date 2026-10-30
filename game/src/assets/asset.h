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
//     "name": "church", "type": "building", "role": "church",   // role: what town/ship generators look up
//     "file": "church.glb",                       // default file for every LOD
//     "lods": [ { "node": "Church_LOD0", "dist": 0 },        // LODs by node name in one file ...
//               { "node": "Church_LOD1", "dist": 40 },        // used from this camera distance (1080p, 45 deg)
//               { "file": "church_far.glb", "px": 24 } ],     // ... or by separate file; "px": used once the
//                                                              //     model is smaller than this on a 1080p screen
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

#ifdef __cplusplus
extern "C" {
#endif

#define ASSET_MAX          256
#define ASSET_MAX_LODS     6      // LOD0..LOD5 (tools/meshy_import.py); the selector allows one more level for the icon
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

// Each LOD after the first says when it takes over: at `dist` world units of camera distance
// (converted to a pixel size for the reference 1080p view at load, so it holds at any
// resolution), or directly at `px` pixels of projected size. One of the two; px wins if both.
typedef struct asset_lod_desc_t { char file[ASSET_PATH]; char node[64]; float dist, px; } asset_lod_desc_t;

typedef struct asset_desc_t {
    char name[64];
    char role[32];                        // what the generators ask for ("church", "warehouse", "hull"...); "" = name
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
    float     dist;                       // use from this camera distance on (0 when the LOD is given in px)
    float     px;                         // or: use once the projected size is below this many reference pixels (0 = from dist)
    int       gpu;                        // renderer-owned flag: 1 once uploaded (verts/idx may then be freed)
    int       queued;                     // an upload job for this LOD is already in flight (asset_upload_lod)
    int       trimmed;                    // its CPU copy was freed un-uploaded (asset_lod0_trim); wanting it reloads the asset
} asset_lod_t;

// UNLOADED -> (asset_request) LOADING -> LOADED | FAILED. Loading runs on the job system
// (core/jobs.h): file reads on the IO lane, glTF parsing + texture decoding + mesh processing on
// the DECODE lane, and the commit (state, LODs, bounds) on the MAIN lane, so the state only ever
// changes on the render thread and nothing here needs a lock. GPU upload follows through the
// hook set by the renderer (asset_set_upload_hook), one budgeted MAIN job per LOD.
typedef enum asset_state_t { ASSET_UNLOADED, ASSET_LOADING, ASSET_LOADED, ASSET_FAILED } asset_state_t;

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
    double loaded_at;                     // diag_now() when it reached ASSET_LOADED (the trim's clock)
    int    stage;                         // while LOADING, which lane holds it: 1 IO read, 2 DECODE build, 3 MAIN commit pending (dashboard only, written by the stage that finishes)
    char  error[256];
} asset_t;

// registry
int      asset_registry_load(const char *manifest_path);   // registers every descriptor listed; returns count (0 + log on error)
int      asset_register_desc(const char *desc_path);       // one descriptor file; returns index or -1
int      asset_count(void);
asset_t *asset_at(int index);
asset_t *asset_find(const char *name);                     // registered asset, loaded or not
asset_t *asset_get_role(asset_type_t type, const char *role);   // first asset of a type with this role that is LOADED; requests unloaded ones; NULL while loading / none / failed
asset_t *asset_get(const char *name);                      // find + request on first use; NULL until LOADED (or if unknown / failed)
void     asset_request(asset_t *a);                        // start loading in the background (no-op unless UNLOADED)
int      asset_request_all(void);                          // request every registered asset; returns how many were started
int      asset_load(asset_t *a);                           // blocking: request + wait. 1 ok; 0 with a->error (idempotent). Main thread only.
void     asset_unload(asset_t *a);                         // frees CPU data, back to ASSET_UNLOADED (waits for a load in flight)
void     asset_unload_all(void);
unsigned asset_generation(void);                           // bumps whenever an asset finishes loading: caches keyed on asset availability (towns) include it in their signature
int      asset_loading_count(void);                        // assets in flight right now (dashboard)
void     asset_pipeline_counts(int *io, int *decode, int *commit, int *upload);   // in flight per stage, plus LOADED assets with LODs still waiting for the GPU (dashboard)
void     asset_set_upload_hook(void (*hook)(asset_t *a, int lod));   // renderer: called on the MAIN lane for each LOD once an asset is LOADED
// Ask for one LOD to reach the GPU (idempotent; no-op once uploaded or already queued). The
// commit queues every LOD but the finest this way, coarsest first; LOD 0 - which for an
// imported scan is the original, only ever drawn when the object fills the screen - is left
// for the first instance that actually picks it, so it costs no VRAM until then.
void     asset_upload_lod(asset_t *a, int lod);
// Free the CPU copy of any LOD that has sat un-uploaded for `idle_s` seconds of wall clock.
// An imported LOD 0 is the original scan - tens of MB per asset, hundreds across a fleet - and
// most of them are never drawn in a session, because LOD 0 only shows when an object fills the
// screen. A trimmed LOD is reloaded (the whole asset is re-requested) the moment something
// picks it, so this costs a stream-in, never the level itself. Call once every few seconds.
void     asset_lod0_trim(double idle_s);
size_t   asset_lod0_trimmed_bytes(void);   // freed this session (dashboard)
void     asset_free_cpu(asset_t *a, int lod);              // renderer: drop the CPU copy after upload
size_t   asset_cpu_bytes(void);                            // total held by the cache (CPU side)
size_t   asset_gpu_bytes(void);                            // total uploaded by the renderer
const char *asset_root(void);                              // resolved assets directory (MARE_ASSETS or game/assets up the tree)

// collision (step 8): the descriptor's shape, derived from the normalised bounds when not
// given. Generators use the footprint to keep pieces apart; nothing physical yet.
float    asset_footprint_radius(const asset_t *a);         // XZ radius about the pivot (sphere, or the AABB's half diagonal)
int      asset_contains_xz(const asset_t *a, float ax, float az, float heading, float px, float pz);   // is world point (px,pz) inside the instance's footprint?

// LOD for a camera distance (world units): the last LOD whose dist * asset_lod_bias <= d.
// The bias is a global quality knob (dashboard): 2 holds detailed LODs twice as far,
// 0.5 drops to coarse LODs at half the authored distance.
int          asset_pick_lod(const asset_t *a, float d);
float        asset_lod_switch_dist(const asset_t *a, int lod);   // camera distance (reference view) from which `lod` is used, bias applied
extern float asset_lod_bias;

// building blocks, exposed for tests and tools
int  asset_desc_parse(const char *json, size_t n, const char *dir, asset_desc_t *d, char *err, size_t errcap);
int  asset_desc_validate(const asset_desc_t *d, char *err, size_t errcap);
int  asset_build(asset_t *a);                              // synchronous build (tools/tests): same as asset_load
const char *asset_type_name(asset_type_t t);

#ifdef __cplusplus
}
#endif

#endif
