// Scene assembly: decides per frame what to draw and at which LOD.
//
//   ships:  full mesh (all ships in line-ahead) above ~80 px on screen, simplified (max 4) above ~26 px, else icon
//   ports:  procedural town (houses by population, tower, fort walls, pier, flag) above ~35 px, else icon
//   map view (mapness > 0.5): everything becomes icons; trade lanes are drawn
// Levels are picked in screen space with hysteresis and cross-fades (gfx/lod.h).
// Towns are regenerated from a per-port seed each frame (cheap, always in sync with the state).

#ifndef MARE_SCENE_H
#define MARE_SCENE_H

#include "../core/types.h"
#include "gpu.h"

typedef struct scene_hints_t {   // filled by the UI each frame before drawing
    int sel_fleet_id, hover_fleet_id, sel_port, hover_port;
    int route_edit;              // the selected fleet's route is being edited (draw it green, open)
    int show_lanes, show_wind, show_regions;
} scene_hints_t;
extern scene_hints_t SH;
extern int scene_culled_ports, scene_culled_ships;   // frustum-culled this frame (dashboard stats)
extern int scene_occluded_ports, scene_occluded_ships; // hidden behind terrain this frame (occlusion.h)
extern int scene_occluded_assets;                    // authored instances hidden by the GPU queries this frame (occquery.h)
int  scene_occlusion_active(void);                   // occlusion tests run this frame (enabled, 3D view, not a staged scene)
extern int scene_asset_instances;                    // authored-asset (GLB) instances drawn this frame
extern int scene_icons_clustered;                    // distant markers merged into a neighbour this frame
extern int scene_assets_evicted;                     // authored assets dropped from the GPU this session
size_t scene_asset_gpu_budget(void);                 // bytes of authored-asset meshes the residency policy keeps (dashboard)
float  scene_asset_idle_s(int asset_index);          // seconds since an instance of this asset was last drawn (-1 never)
int scene_asset_lod_count(int asset_index, int lod); // of those, instances of one asset at one LOD (dashboard)
struct asset_t; struct mesh_t;
struct mesh_t *scene_asset_mesh(struct asset_t *a, int lod);  // GPU mesh of an asset LOD, uploaded on first use (shared with detailview.c)
void scene_asset_upload(struct asset_t *a, int lod);          // asset.c upload hook (MAIN lane): same as above, budgeted per frame
// authored-asset instance lists, shared with the detail view: begin, push instances (LOD picked
// in screen space under `key`, see lod.h), end (uploads instance buffers), submit (draws)
void scene_assets_begin(void);
void scene_asset_instance(struct asset_t *a, inst_t in, unsigned key, float camdist);
void scene_assets_end(void);
int  scene_assets_submit(void);

void scene_fleet_pos(const fleet_t *f, float *x, float *z, float *heading);   // draw position (docked fleets are moored around the harbour)
void scene_reset_caches(void);    // also resets the fleet movement animation
void scene_reset_anim(void);      // drop smoothed fleet positions (new game / load / teleport)    // call after game_new/game_load: town instance caches depend on world heights and port state
// Geometry is built once per frame, then submitted with whatever pipeline is bound: the shadow
// cascades reuse the same instance lists as the main pass (lighting.c, mare.c).
void scene_build_ports(void);     // towns are cached per port and rebuilt when size/fort/owner/population change
void scene_build_ships(void);
int  scene_submit_ports(void);    // after meshes_bind(); returns instances drawn
int  scene_submit_ships(void);    // after meshes_bind()
int  scene_submit_smoke(void);    // after meshes_bind_smoke(), once the sea is down
int  scene_submit_shadow(void);   // after meshes_bind_depth(): towns + ships as shadow casters
void scene_push_icons(void);      // icons for far/map-view ports, fleets and storms (between icons_begin/icons_draw)
void scene_draw_overlays(void);   // debug-draw lines: selected path, routes, lanes, storms, wind arrows (flushed by dd_flush)

#endif
