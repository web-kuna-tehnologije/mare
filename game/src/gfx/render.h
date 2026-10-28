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
    vec3  campos, sun, sky;         // camera position, sun direction (unit), sky/fog colour (sRGB)
    float time;                     // seconds, for animation
    float mapness;                  // 0 = 3D view, 1 = parchment map view (camera.h)
    float flatten;                  // 0..1 flattens terrain heights (map view)
    float daylight;                 // global light multiplier (1 = day)
    vec3  suncol;                   // linear sun radiance (sun colour * intensity * daylight)
    vec3  suntint;                  // sun colour multiplier (warm at dawn/dusk, blue at night)
    vec3  ambsky, ambgnd;           // linear hemispheric ambient (sky above, bounce below)
    vec3  sea_center;               // xz the sea's detail grid centres on (the camera's look-at point)
    float tod;                      // time of day 0..1 (0 = midnight); drives the sun when the cycle is on
    float ambfloor;                 // 0 at night .. 1 in full day; keeps the ambient above a moonlit floor
} render_frame_t;
extern render_frame_t RF;
extern int screen_w, screen_h;      // framebuffer size, sampled once per frame (v3's app_width() queries X11 and GL every call)
extern int window_w, window_h;      // window size in screen coordinates (differs from the framebuffer on HiDPI)

void render_init(void);             // build programs, LOD grids, textures, procedural meshes (after world_init)
void render_territory_upload(void); // push the territory map to the GPU when territory_dirty == 2

// procedural detail normal maps (detail.c): tileable wave normals for the sea, surface grain
// applied triplanar to the meshes (they carry no UVs)
void detail_init(void);
float terrain_max_height(void);          // highest heightmap value (occlusion ray early-out)
const rhi_texture_t *detail_waves(void);
const rhi_texture_t *detail_grain(void);

// streamed textures (textures.c): request once, poll each frame; NULL until resident
int  texture_request(const char *path, rhi_format_t fmt, rhi_filter_t filter, rhi_wrap_t wrap);
const rhi_texture_t *texture_get(int handle);
int  texture_state(int handle);          // 1 loading, 2 ready, 3 failed
int  texture_count(void);                // streamed textures requested so far (dashboard)
const char *texture_path(int handle);
void texture_size(int handle, int *w, int *h);   // 0x0 until decoded

// HDR (hdr.c): 3D passes render into an RGBA16F target between hdr_begin and hdr_resolve
void hdr_begin(int w, int h, float r, float g, float b);   // bind + clear the HDR target (recreated on resize)
void hdr_resolve(int w, int h);                            // tonemap to the backbuffer; UI draws after this
extern float hdr_exposure;                                 // tonemap multiplier (1 = neutral)

// lighting & shadows (lighting.c): one directional sun shared by every 3D pass
void  render_light_update(void);         // fills RF.suncol/ambsky/ambgnd from RF.sun and RF.daylight
// Day/night: turns a time of day into the sun direction, daylight level and sky colour, so the
// shadows actually move. Pinned by MARE_SUN, disabled by MARE_DAYNIGHT=0 (both keep the old
// fixed afternoon sun, which is what the screenshot comparisons use).
void  render_sun_update(float day);
extern int   daynight_enabled;
extern float daynight_period_days;       // simulated days per full cycle
void  light_bind(const rhi_pipeline_t *p);  // sets the GLSL_LIGHT uniforms (sun, ambient, shadow atlas)
void  shadow_render(void);               // renders the 3 cascades of the sun shadow atlas (before the main passes)
extern int shadow_enabled;               // 0 disables the shadow pass and the lookup
extern int shadow_casters_drawn;         // stats for the dashboard (ships/towns; the terrain only receives)
vec3  srgb_to_linear3(vec3 c);           // CPU-side colour conversion (clear colours, debug-draw)
unsigned rgb_linear(unsigned srgb_col);  // packed sRGB colour -> packed linear (for v3 debug draw)

// passes
void terrain_draw(float planes[6][4]);   // frustum planes from cam_frustum(); chunks pick a LOD by distance
void sea_draw(void);                     // world grid (flat) + the camera-centred wave grid
void sea_draw_local(float depth, float scale, float radius);   // staged scenes: the wave grid alone at a constant depth
void sea_update_waves(float day, float dt);   // once per frame: derive the wave set from the wind (DESIGN_VISUAL.md step 4)
void sea_bind_waves(const rhi_pipeline_t *p); // upload it to a pipeline built with GLSL_WAVES
extern float sea_wind_dx, sea_wind_dz;   // wind direction the current wave set was built from
extern float sea_wind_str;               // and its strength
extern float sea_wind_amp;               // wave height factor of the set(s) on screen
extern float sea_wind_now_str;           // wind strength right now at the look-at point (weather)
extern int   sea_set_changes;            // wave sets built this run (a pan across wind bands cross-fades, never drifts)
extern int   sea_nyquist;                // 1 = fade waves finer than the detail grid (MARE_SEA_NYQUIST=0 to compare)
extern float sea_wave_amp;               // wave height knob (1 = normal, 0 = flat; MARE_WAVES / dashboard)
extern float sea_wave_scale;             // world units per wave unit: 1 on the map, DV_SHIP_SCALE in a staged scene
extern float sea_local_depth;            // >0 makes every wave lookup use this depth (staged scenes have no heightmap)
extern float sea_clip_z; extern int sea_clip_on;   // staged scenes: the shoreline, as a world-z plane
void meshes_bind(void);             // bind the instanced-mesh program (call before mesh_draw / scene_draw_*)
void meshes_bind_depth(const mat44 *lightvp);   // depth-only instanced pipeline for the shadow pass
void meshes_bind_smoke(void);       // translucent instanced pipeline for gunsmoke (after sea_draw)
extern int terrain_chunks_drawn, terrain_tris_drawn;   // stats for the dashboard
extern int terrain_show_regions;                       // map view: shade hinterlands by development and draw their borders

// procedural meshes built at init
extern mesh_t ship_mesh[SHIP_COUNT][2];   // [ship type][lod]: 0 = full hull/masts/sails/stripe/flag, 1 = simplified
extern mesh_t mesh_house, mesh_roof, mesh_tower, mesh_wall, mesh_pier, mesh_flagpole;
extern mesh_t mesh_puff;   // gunsmoke
float ship_scale(int type);               // world scale of a ship type (sloop 0.7 ... ship of the line 1.55, times 1.35)

// screen-sized SDF icons at world positions (ports/fleets/storms when far away or in map view)
enum { ICON_DISC=0, ICON_RING, ICON_ARROW, ICON_DIAMOND, ICON_SQUARE, ICON_STORM, ICON_CROSS };
typedef struct icon_t { float x, y, z, heading; float size_px, shape, alpha, outline; float r, g, b, pad; } icon_t;
icon_t *icon_push(void);                  // append for this frame (cleared by icons_begin)
void    icons_begin(void);
void    icons_draw(void);                 // depth test off, alpha blended

#endif
