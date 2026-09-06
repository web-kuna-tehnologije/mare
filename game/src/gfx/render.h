// Renderer entry points.
//
// Per-frame parameters are written once into `RF` (and the framebuffer/window sizes into the
// globals below) and read by every pass. Pass order in mare.c: terrain -> ports/ships (instanced
// meshes) -> sea (alpha blended) -> debug-draw overlays -> icons -> UI.

#ifndef MARE_RENDER_H
#define MARE_RENDER_H

#include "gpu.h"

typedef struct render_frame_t {
    mat44 vp;                       // view * projection (v3 row-vector convention)
    vec3  campos, sun, sky;         // camera position, sun direction (unit), sky/fog colour
    float time;                     // seconds, for animation
    float mapness;                  // 0 = 3D view, 1 = parchment map view (camera.h)
    float flatten;                  // 0..1 flattens terrain heights (map view)
    float daylight;                 // global light multiplier (1 = day)
} render_frame_t;
extern render_frame_t RF;
extern int screen_w, screen_h;      // framebuffer size, sampled once per frame (v3's app_width() queries X11 and GL every call)
extern int window_w, window_h;      // window size in screen coordinates (differs from the framebuffer on HiDPI)

void render_init(void);             // build programs, LOD grids, textures, procedural meshes (after world_init)
void render_territory_upload(void); // push the territory map to the GPU when territory_dirty == 2

// passes
void terrain_draw(float planes[6][4]);   // frustum planes from cam_frustum(); chunks pick a LOD by distance
void sea_draw(void);
void meshes_bind(void);             // bind the instanced-mesh program (call before mesh_draw / scene_draw_*)
extern int terrain_chunks_drawn, terrain_tris_drawn;   // stats for the dashboard
extern int terrain_show_regions;                       // map view: shade hinterlands by development and draw their borders

// procedural meshes built at init
extern mesh_t ship_mesh[SHIP_COUNT][2];   // [ship type][lod]: 0 = full hull/masts/sails/stripe/flag, 1 = simplified
extern mesh_t mesh_house, mesh_roof, mesh_tower, mesh_wall, mesh_pier, mesh_flagpole;
float ship_scale(int type);               // world scale of a ship type (sloop 0.7 ... ship of the line 1.55, times 1.35)

// screen-sized SDF icons at world positions (ports/fleets/storms when far away or in map view)
enum { ICON_DISC=0, ICON_RING, ICON_ARROW, ICON_DIAMOND, ICON_SQUARE, ICON_STORM, ICON_CROSS };
typedef struct icon_t { float x, y, z, heading; float size_px, shape, alpha, outline; float r, g, b, pad; } icon_t;
icon_t *icon_push(void);                  // append for this frame (cleared by icons_begin)
void    icons_begin(void);
void    icons_draw(void);                 // depth test off, alpha blended

#endif
