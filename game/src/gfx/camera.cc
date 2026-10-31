// mare::engine::render::Camera (see camera.h): RTS camera - smoothing, pitch/mapness from
// distance, matrices, picking, frustum. The camera state itself stays the C global `cam`
// (camera.h) because every pass reads it; this class groups the behaviour.
#include "../platform/v3cpp.hh"
#include <cmath>

extern "C" {
#include "camera.h"
#include "render.h"
#include "../core/types.h"
#include "../core/util.h"
}

rts_cam_t cam;
float cam_pitch_scale = 1.f;
float cam_pitch_user = 0.f;
float cam_min_eye_y = -1e9f;

namespace mare::engine::render {

class Camera {
public:
    void  reset(float tx, float tz, float dist);
    void  go_to(float x, float z);                    // cam_goto: recentre
    void  clamp();
    void  update(float dt);
    int   pick_ground(float mx, float my, float h, float *wx, float *wz);
    int   project(float wx, float wy, float wz, float *sx, float *sy);
    mat44 proj_to_gl(mat44 proj);
    void  frustum(float planes[6][4]);
    int   sphere_visible(const float planes[6][4], float x, float y, float z, float r);

private:
    static float pitch_for(float dist);
};

Camera &camera() { static Camera c; return c; }

// pitch flattens towards street level as the city view approaches, so a zoomed-in town reads
// like a citybuilder scene rather than a top-down map tile
float Camera::pitch_for(float dist) {
    float t = powf(clampf((dist - CAM_MIN_DIST) / 420.f, 0, 1), 0.55f);
    float base = 28.f - 10.f * clampf((CAM_CITY_DIST - dist) / (CAM_CITY_DIST - CAM_MIN_DIST), 0, 1);
    float deg = maxf(12.f, (base + 60.f * t) * cam_pitch_scale) + cam_pitch_user;
    return clampf(deg, CAM_PITCH_MIN, CAM_PITCH_MAX) * 0.0174533f;
}

void Camera::reset(float tx, float tz, float dist) {
    cam.tx = cam.want_tx = tx; cam.tz = cam.want_tz = tz; cam.dist = cam.want_dist = dist; cam.yaw = cam.want_yaw = 0; cam.follow_fleet_id = 0;
    update(1);
}
void Camera::go_to(float x, float z) { cam.want_tx = x; cam.want_tz = z; if (cam.want_dist > 120) cam.want_dist = 60; cam.follow_fleet_id = 0; }
void Camera::clamp() {
    cam.want_tx = clampf(cam.want_tx, lon2x(LON0)+4, lon2x(LON1)-4);
    cam.want_tz = clampf(cam.want_tz, lat2z(LAT1)+4, lat2z(LAT0)-4);
    cam.want_dist = clampf(cam.want_dist, CAM_MIN_DIST, CAM_MAX_DIST);
}

// vecmath's perspective/ortho put clip z in [0,1] (D3D convention); GL maps NDC [-1,1] to the
// depth buffer, so that convention only ever used window depth [0.5,1]. Post-multiplying with
// z' = 2z - w restores the full range (row-vector convention, so R is applied last).
mat44 Camera::proj_to_gl(mat44 proj) {
    mat44 r = mat44_identity();
    r.z.z = 2; r.w.z = -1;
    return mat44_mul_mat44(proj, r);
}

void Camera::update(float dt) {
    clamp();
    float k = 1 - expf(-dt * 10.f);
    cam.tx += (cam.want_tx - cam.tx) * k; cam.tz += (cam.want_tz - cam.tz) * k;
    cam.dist += (cam.want_dist - cam.dist) * k; cam.yaw += (cam.want_yaw - cam.yaw) * k;
    cam.pitch = pitch_for(cam.dist);
    float m = clampf((cam.dist - CAM_MAP_START) / (CAM_MAP_FULL - CAM_MAP_START), 0, 1);
    cam.mapness = m * m * (3 - 2*m);
    float gx = sinf(cam.yaw), gz = -cosf(cam.yaw), cp = cosf(cam.pitch), sp = sinf(cam.pitch);
    vec3 target = vec3(cam.tx, 0, cam.tz);
    cam.pos = vec3(cam.tx - gx*cam.dist*cp, cam.dist*sp, cam.tz - gz*cam.dist*cp);
    // Down at water level a crest would put the eye under the surface: lift it (the look-at
    // point stays, so the view tilts down a little) rather than let the sea swallow the view.
    if (cam.pos.y < cam_min_eye_y) cam.pos.y = cam_min_eye_y;
    cam.fwd = norm3(sub3(target, cam.pos));
    cam.right = norm3(cross3(cam.fwd, vec3(0,1,0)));
    cam.up = cross3(cam.right, cam.fwd);
    // The far plane follows the pitch: looking down, five camera distances past the target is
    // all the ground there is; tilted towards the horizon the view runs out over the sea and
    // the plane has to go with it (0.22) or the world ends in a hard line.
    float level = 1.f - clampf((cam.pitch * 57.2958f - CAM_PITCH_MIN) / 22.f, 0.f, 1.f);
    cam.znear = maxf(0.3f, cam.dist * 0.02f); cam.zfar = cam.dist * 5.f + 1200.f + 3200.f * level;
    float aspect = screen_w / maxf(1.f, (float)screen_h);
    cam.view = lookat44(cam.pos, target, vec3(0,1,0));
    cam.proj = proj_to_gl(perspective44(45.f * 0.0174533f, aspect, cam.znear, cam.zfar));
    cam.vp = mat44_mul_mat44(cam.view, cam.proj);
    float det; if (!mat44_inverse(&cam.inv_vp, &det, cam.vp)) cam.inv_vp = mat44_identity();
}

// v3's vecmath uses row vectors (v*M), so v*M == mat44_mul_vec4(M^T, v)
int Camera::pick_ground(float mx, float my, float h, float *wx, float *wz) {
    float nx = (mx / screen_w) * 2 - 1, ny = 1 - (my / screen_h) * 2;
    mat44 it = mat44_transpose(cam.inv_vp);
    vec4 a = mat44_mul_vec4(it, vec4(nx, ny, -1, 1)), b = mat44_mul_vec4(it, vec4(nx, ny, 1, 1));
    vec3 p0 = vec3(a.x/a.w, a.y/a.w, a.z/a.w), d = sub3(vec3(b.x/b.w, b.y/b.w, b.z/b.w), p0);
    if (fabsf(d.y) < 1e-6f) return 0;
    float t = (h - p0.y) / d.y; if (t < 0) return 0;
    *wx = p0.x + d.x*t; *wz = p0.z + d.z*t;
    return 1;
}
int Camera::project(float wx, float wy, float wz, float *sx, float *sy) {
    vec4 c = mat44_mul_vec4(mat44_transpose(cam.vp), vec4(wx, wy, wz, 1));
    if (c.w <= 0.001f) return 0;
    *sx = (c.x / c.w * 0.5f + 0.5f) * screen_w; *sy = (1 - (c.y / c.w * 0.5f + 0.5f)) * screen_h;
    return 1;
}
int Camera::sphere_visible(const float planes[6][4], float x, float y, float z, float r) {
    for (int i = 0; i < 6; ++i) if (planes[i][0]*x + planes[i][1]*y + planes[i][2]*z + planes[i][3] < -r) return 0;
    return 1;
}
void Camera::frustum(float planes[6][4]) {
    mat44 m = cam.vp;
    float c[4][4] = { { m.x.x, m.y.x, m.z.x, m.w.x }, { m.x.y, m.y.y, m.z.y, m.w.y }, { m.x.z, m.y.z, m.z.z, m.w.z }, { m.x.w, m.y.w, m.z.w, m.w.w } };
    for (int i = 0; i < 4; ++i) {
        planes[0][i] = c[3][i] + c[0][i]; planes[1][i] = c[3][i] - c[0][i];
        planes[2][i] = c[3][i] + c[1][i]; planes[3][i] = c[3][i] - c[1][i];
        planes[4][i] = c[3][i] + c[2][i]; planes[5][i] = c[3][i] - c[2][i];
    }
}

} // namespace mare::engine::render

// C ABI (camera.h) for the unported callers
extern "C" {
void  cam_reset(float tx, float tz, float dist) { mare::engine::render::camera().reset(tx, tz, dist); }
void  cam_goto(float x, float z) { mare::engine::render::camera().go_to(x, z); }
void  cam_clamp(void) { mare::engine::render::camera().clamp(); }
void  cam_update(float dt) { mare::engine::render::camera().update(dt); }
int   cam_pick_ground(float mx, float my, float h, float *wx, float *wz) { return mare::engine::render::camera().pick_ground(mx, my, h, wx, wz); }
int   cam_project(float wx, float wy, float wz, float *sx, float *sy) { return mare::engine::render::camera().project(wx, wy, wz, sx, sy); }
mat44 cam_proj_to_gl(mat44 proj) { return mare::engine::render::camera().proj_to_gl(proj); }
void  cam_frustum(float planes[6][4]) { mare::engine::render::camera().frustum(planes); }
int   cam_sphere_visible(const float planes[6][4], float x, float y, float z, float r) { return mare::engine::render::camera().sphere_visible(planes, x, y, z, r); }
}
