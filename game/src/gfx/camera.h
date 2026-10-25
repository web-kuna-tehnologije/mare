// RTS camera: orbits a ground target; the pitch steepens and the view morphs into a map as you
// zoom out (cam.mapness goes 0 -> 1 between CAM_MAP_START and CAM_MAP_FULL). Pure state + math:
// input handling lives in ui/input.c. Matrices use v3's row-vector convention (v * M).

#ifndef MARE_CAMERA_H
#define MARE_CAMERA_H

typedef struct rts_cam_t {
    float tx, tz, dist, yaw;                    // current ground target, distance, yaw (0 = north up)
    float want_tx, want_tz, want_dist, want_yaw;// targets (smoothed towards by cam_update)
    float pitch, mapness;                       // derived
    vec3 pos, fwd, right, up;
    mat44 view, proj, vp, inv_vp;
    float znear, zfar;                          // scale with distance to keep depth precision
    int follow_fleet_id;                        // 0 = none (fleet id, not index)
} rts_cam_t;

extern rts_cam_t cam;

#define CAM_MIN_DIST   2.5f    // close enough to walk a town square (city view)
#define CAM_MAX_DIST   950.f
#define CAM_CITY_DIST  16.f    // below this over a town: city view (mare.c slows the clock, hud.c says so)
#define CAM_MAP_START  300.f   // map styling begins
#define CAM_MAP_FULL   480.f   // fully map view

void cam_reset(float tx, float tz, float dist);   // snap to a target (no smoothing)
void cam_goto(float x, float z);                  // recentre; zooms in to 60 if currently in map view
void cam_clamp(void);                             // keep targets inside the world
void cam_update(float dt);                        // smooth towards targets and rebuild matrices (every frame)
int  cam_pick_ground(float mx, float my, float h, float *wx, float *wz);   // framebuffer pixel -> plane y=h; 0 if the ray misses
int  cam_project(float wx, float wy, float wz, float *sx, float *sy);      // world -> framebuffer pixels; 0 if behind the camera
mat44 cam_proj_to_gl(mat44 proj);                 // remap clip z from [0,1] (vecmath's D3D style) to GL's [-1,1]: uses the whole depth buffer instead of half, visibly reducing z-fighting
void cam_frustum(float planes[6][4]);             // clip planes (ax+by+cz+d >= 0 inside) for culling
int  cam_sphere_visible(const float planes[6][4], float x, float y, float z, float r);   // shared culling helper (terrain uses its AABB variant)

#endif
