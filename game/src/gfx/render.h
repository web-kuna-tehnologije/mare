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
    float fog_density, fog_start;   // weather (gfx/weather.c): fog multiplier and the distance it starts at (1, 250 in clear air)
    float calm, overcast;           // weather: glassy-sea factor, storm-overhead factor
    float vfog_density, vfog_height, vfog_sun;   // volumetric fog (weather.c -> hdr.c): density at sea level, scale height, sun in-scatter
    vec3  vfog_color;               // its colour, sRGB
} render_frame_t;
extern render_frame_t RF;
extern int screen_w, screen_h;      // framebuffer size, sampled once per frame (v3's app_width() queries X11 and GL every call)
extern int window_w, window_h;      // window size in screen coordinates (differs from the framebuffer on HiDPI)

void render_init(void);             // build programs, LOD grids, textures, procedural meshes (after world_init)
void render_territory_upload(void); // push the territory map to the GPU when territory_dirty == 2

// world streaming view (streamview.c, game/docs/DESIGN_STREAMING.md): drives world/stream.h
// from the camera, composes the terrain detail window, debug overlay, metrics
void streamview_update(void);                       // once per frame, before the terrain draws
void streamview_push_tiles(void);                   // authored tile meshes (inside the scene_assets bracket)
void streamview_draw_debug(void);                   // tile grid overlay (dd lines; stream_show_tiles)
const rhi_texture_t *streamview_detail_tex(void);   // NULL until a window was composed
void streamview_detail_rect(float out[4]);          // x0, z0, 1/extent, 1/extent (world)
double streamview_window_ms(void);
int    streamview_unmodeled(void);                  // wanted tactical chunks without an authored mesh
extern int stream_show_tiles;                       // dashboard toggle

// procedural detail normal maps (detail.c): tileable wave normals for the sea, surface grain
// applied triplanar to the meshes (they carry no UVs)
void detail_init(void);
float terrain_max_height(void);          // highest heightmap value (occlusion ray early-out)
extern int terrain_ao_on;                // baked horizon occlusion on the terrain's ambient (MARE_TERRAIN_AO=0)
// sky and clouds (sky.c, DESIGN_VISUAL.md step 8)
void sky_update(float dt);                    // cover from the weather, drift with the wind
void sky_draw(void);                          // first thing into the HDR target: gradient, sun disc, cloud layer
void sky_draw_overhead(void);                 // after the scene: the cloud layer seen from above, over the map
void sky_bind_clouds(const rhi_pipeline_t *p);// u_cloud for GLSL_CLOUDS users (light_bind calls it)
extern float sky_cloud_cover; extern int sky_enabled;
// ambient occlusion (step 9): half-resolution SSAO from the HDR depth, applied in the tonemap
void hdr_ao_pass(void);                       // after the opaque passes and the water, before hdr_resolve
void hdr_bloom_pass(void);                    // bright pass + blur, before hdr_resolve
extern float hdr_bloom; extern int hdr_bloom_enabled;
extern float hdr_ssao_strength;               // 0 = off (MARE_SSAO), 1 = full
extern int   hdr_ssao_enabled;
void hdr_capture_scene(void);                 // copy the HDR colour so far (before the water) for refraction
const rhi_texture_t *hdr_scene_copy(void);    // that copy (NULL before the first capture)
const rhi_texture_t *hdr_depth(void);         // the HDR target's depth attachment
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
extern float shadow_radius;              // casters are taken from this distance around the camera, and the cascades cover it
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
extern float sea_foam;                   // foam strength knob (1 normal, 0 none; MARE_FOAM)
extern int   sea_refract;                // refract the frame behind the water (MARE_SEA_REFRACT=0 for the old alpha blend)
extern int   sea_inner_on;               // fine inner patch lattice near the look-at point (MARE_SEA_INNER=0)
float sea_height_at(float x, float z);   // wave surface height at a world point this frame (CPU mirror of the shader sum)
void sea_set_ships(const float *xzhs, const float *len, int n);   // ships to foam around this frame (x, z, heading, speed; hull length), max 24
extern int   sea_tess_on;                // tessellated patches available (GL 4.0; MARE_SEA_TESS=0 forces the warped grid)
extern int   sea_tess_now;               // ... and in use this frame (only within SEA_TESS_DIST of the water)
extern int   sea_tess_level_max;         // finest subdivision per patch edge (dashboard)
extern float sea_wave_amp;               // wave height knob (1 = normal, 0 = flat; MARE_WAVES / dashboard)
float sea_zoom_amp(void);                // zoom factor on the wave height: 1 close in, flattening as the view pulls out
float sea_amp_now(void);                 // the full amplitude this frame (knob x wind x map morph x zoom); meshes ride the same field
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
