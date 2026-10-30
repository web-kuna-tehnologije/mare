// mare::engine::render::Lighting (see render.h): directional sun + cascaded shadow maps
// (DESIGN_RENDERER.md steps 5 and 6).
//
// One sun lights every 3D pass: render_light_update() derives its linear radiance and the
// hemispheric ambient from RF.sun/RF.daylight, and light_bind() uploads them (plus the shadow
// atlas) to any pipeline built with GLSL_LIGHT.
//
// Shadows: three cascades covering the near part of the camera frustum, packed side by side in
// one depth atlas (3*SHADOW_RES x SHADOW_RES). Each cascade fits a bounding sphere of its
// frustum slice's ground footprint, so the projection is rotation-invariant, and its centre is
// snapped to texel increments so shadows don't shimmer when the camera moves.
//
// Only the *objects* cast: ships, houses, roofs, towers and fort walls. The terrain receives
// shadows but is not rendered into the depth map. Its 374 chunks are coarse, LOD-switched and
// skirted, so as occluders they produce LOD-mismatch bands, skirt stripes and self-shadow acne,
// while contributing almost nothing: terrain relief is already shaded accurately by the N.L term
// from the heightmap normals, which are per-pixel rather than per-chunk-LOD. Mountains therefore
// do not cast shadows into valleys (noted in KNOWN_ISSUES).

#include "../platform/v3cpp.hh"
#include <cmath>
#include <cstdlib>
#include <cstdio>

extern "C" {
#include "render.h"
#include "camera.h"
#include "scene.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../core/balance.h"
}

#define SHADOW_RES    1024
#define SHADOW_CASCADES 3

int shadow_enabled = 1;
int shadow_casters_drawn;
// How far from the camera casters are taken, and how far the cascades reach (DESIGN_VISUAL.md
// step 10). It used to be cam.dist * 1.2 + 40, capped at 300: in the city view that is 57 units,
// so a building a street away cast nothing. The cascades now cover the distance at which meshes
// are still drawn, and scene.c collects casters from the same radius - and only from it.
float shadow_radius = 300.f;

// ---------------------------------------------------------------------------
// day and night
//
// The sun rides a tilted circle once per `daynight_period_days` simulated days, so shadows
// sweep across the world while the game runs (and stop when it is paused, since they are driven
// by G.day rather than by wall time). Three things follow the elevation: the light direction,
// the daylight level and the sky colour, with a warm tint at the horizon.
//
// Two deliberate departures from physics. The elevation is clamped to DAYNIGHT_MIN_ELEV so the
// shadow cascades never degenerate into the plane of the ground, and night bottoms out at
// DAYNIGHT_FLOOR rather than at black: this is a strategy map that has to stay readable, so
// "night" is a dim blue dusk. MARE_SUN pins the direction (screenshot tests) and
// MARE_DAYNIGHT=0 keeps the old fixed afternoon sun.

int   daynight_enabled = 1;
float daynight_period_days = DAYNIGHT_PERIOD_DAYS;

namespace mare::engine::render {

class Lighting {
public:
    vec3     to_linear3(vec3 c);                     // srgb_to_linear3
    unsigned rgb_linear(unsigned col);
    void     light_update();                         // render_light_update
    void     sun_update(float day);                  // render_sun_update
    void     bind(const rhi_pipeline_t *p);          // light_bind
    void     shadow_dump(const char *path);
    void     shadow_render();

private:
    rhi_target_t shadow_atlas;
    mat44 cascade_vp[SHADOW_CASCADES];
    float cascade_split[SHADOW_CASCADES];      // view-distance at which each cascade ends
    float cascade_wtexel[SHADOW_CASCADES];     // world size of one shadow texel per cascade

    static float srgb_to_linear_f(float c);
    static void  slice_sphere(float dnear, float dfar, vec3 *center, float *radius);
    static mat44 cascade_matrix(vec3 center, float radius);
};

Lighting &lighting() { static Lighting l; return l; }

// ---------------------------------------------------------------------------
// colour helpers (CPU side)

float Lighting::srgb_to_linear_f(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
vec3 Lighting::to_linear3(vec3 c) { return vec3(srgb_to_linear_f(c.x), srgb_to_linear_f(c.y), srgb_to_linear_f(c.z)); }

// packed 0xAARRGGBB sRGB -> packed linear, for colours handed to v3's debug draw (which writes
// straight into the linear HDR target). 8-bit linear banding is acceptable for overlay lines.
unsigned Lighting::rgb_linear(unsigned col) {   // colours are 0xAABBGGRR (util.h)
    unsigned a = col & 0xFF000000u;
    unsigned r = (unsigned)(clampf(srgb_to_linear_f(col_r(col)), 0, 1) * 255.f + 0.5f);
    unsigned g = (unsigned)(clampf(srgb_to_linear_f(col_g(col)), 0, 1) * 255.f + 0.5f);
    unsigned b = (unsigned)(clampf(srgb_to_linear_f(col_b(col)), 0, 1) * 255.f + 0.5f);
    return a | (b << 16) | (g << 8) | r;
}

// ---------------------------------------------------------------------------
// the sun

void Lighting::light_update() {
    float d = RF.daylight;
    vec3 tint = RF.suntint.x > 0 || RF.suntint.y > 0 || RF.suntint.z > 0 ? RF.suntint : vec3(1,1,1);
    // The ambient takes its colour from the sky, which at night is almost black - multiplying
    // that by a low daylight level would double-darken the scene into an unplayable map. Keep a
    // moonlit floor instead: the sky still reads as night, the ground stays legible.
    vec3 ambcol = lerp3(vec3(0.30f, 0.37f, 0.52f), RF.sky, clampf(RF.ambfloor, 0.f, 1.f));
    float ambd = lerpf(0.72f, 1.f, clampf(RF.ambfloor, 0.f, 1.f));
    // Sun radiance is tuned so a mid-grey albedo under full sun lands near 0.6 after tonemapping.
    vec3 base = srgb_to_linear3(vec3(1.00f, 0.96f, 0.88f));
    RF.suncol = scale3(vec3(base.x * tint.x, base.y * tint.y, base.z * tint.z), 3.1f * d * (1.f - 0.6f * RF.overcast));   // a storm overhead dims the sun (weather.c)
    // Hemispheric ambient: sky from above, warm bounce from the ground below.
    RF.ambsky = scale3(srgb_to_linear3(ambcol), 0.55f * ambd);
    RF.ambgnd = scale3(srgb_to_linear3(vec3(0.33f, 0.30f, 0.25f)), 0.30f * ambd);
}

void Lighting::sun_update(float day) {
    static const char *sun_env; static int sun_read;
    if (!sun_read) {
        sun_read = 1;
        sun_env = getenv("MARE_SUN");
        const char *dn = getenv("MARE_DAYNIGHT");
        if (dn) daynight_enabled = atoi(dn) != 0;
        const char *pd = getenv("MARE_DAYNIGHT_DAYS");
        if (pd) daynight_period_days = maxf(0.5f, (float)atof(pd));
        if (sun_env) LOG_INFO("gpu", "sun pinned to %s (MARE_SUN)", sun_env);
        else LOG_INFO("gpu", "day/night cycle %s (%.0f simulated days per cycle)", daynight_enabled ? "on" : "off", daynight_period_days);
    }
    float tod = DAYNIGHT_NOON;                                   // the fixed afternoon sun of 0.7.x
    // Day 0 is mid-morning, not midnight: a new game should open in daylight.
    if (daynight_enabled && !sun_env) { tod = DAYNIGHT_START + day / daynight_period_days; tod -= floorf(tod); }
    RF.tod = tod;

    float a = (tod - 0.25f) * 6.2831853f;                        // 06:00 sunrise, 12:00 noon, 18:00 sunset
    float elev = sinf(a), east = cosf(a);
    // direction towards the sun: it swings east -> west and keeps a southern tilt (+z is south)
    vec3 dir = norm3(vec3(east * 0.85f, maxf(elev, DAYNIGHT_MIN_ELEV), 0.35f));
    if (sun_env) { float sx = 0.45f, sy = 0.75f, sz = 0.35f; sscanf(sun_env, "%f,%f,%f", &sx, &sy, &sz); dir = norm3(vec3(sx, maxf(sy, 0.08f), sz)); }
    RF.sun = dir;

    // daylight: full above ~15 degrees, down to the night floor once the sun is below the horizon
    float lit = clampf((elev + 0.16f) / 0.42f, 0.f, 1.f);
    lit = lit * lit * (3.f - 2.f * lit);
    RF.daylight = lerpf(DAYNIGHT_FLOOR, 1.f, lit);
    RF.ambfloor = lit;                                           // render_light_update keeps the ambient above a floor
    // horizon warmth: strongest while the sun is within a few degrees of the horizon
    float horizon = clampf(1.f - fabsf(elev) * 3.2f, 0.f, 1.f);
    vec3 day_tint = vec3(1.f, 1.f, 1.f), dusk_tint = vec3(1.25f, 0.72f, 0.42f), night_tint = vec3(0.55f, 0.68f, 1.05f);
    RF.suntint = lerp3(lerp3(day_tint, dusk_tint, horizon), night_tint, 1.f - lit);
    // sky: day blue -> sunset orange -> night blue (mare.c blends the map parchment over this)
    vec3 sky_day = vec3(0.60f, 0.74f, 0.88f), sky_dusk = vec3(0.86f, 0.60f, 0.42f), sky_night = vec3(0.07f, 0.10f, 0.20f);
    RF.sky = lerp3(lerp3(sky_night, sky_day, lit), sky_dusk, horizon * (0.35f + 0.65f * lit));
}

void Lighting::bind(const rhi_pipeline_t *p) {
    rhi_set_f2(rhi_uniform(p, "u_fog"), RF.fog_density > 0 ? RF.fog_density : 1.f, RF.fog_start > 0 ? RF.fog_start : 250.f);
    sky_bind_clouds(p);
    rhi_set_f3(rhi_uniform(p, "u_sun"), RF.sun.x, RF.sun.y, RF.sun.z);
    rhi_set_f3(rhi_uniform(p, "u_suncol"), RF.suncol.x, RF.suncol.y, RF.suncol.z);
    rhi_set_f3(rhi_uniform(p, "u_ambsky"), RF.ambsky.x, RF.ambsky.y, RF.ambsky.z);
    rhi_set_f3(rhi_uniform(p, "u_ambgnd"), RF.ambgnd.x, RF.ambgnd.y, RF.ambgnd.z);
    int on = shadow_enabled && shadow_atlas.fbo && RF.mapness < 0.98f;
    static int dbg = -1; if (dbg < 0) { const char *e = getenv("MARE_SHADOW_DEBUG"); dbg = e ? atoi(e) : 0; }
    rhi_set_f1(rhi_uniform(p, "u_shadow_debug"), (float)dbg);
    rhi_set_f1(rhi_uniform(p, "u_shadow_on"), on ? 1.f : 0.f);
    if (!on) return;
    rhi_set_i1(rhi_uniform(p, "u_shadowmap"), 3);
    rhi_texture_bind(3, &shadow_atlas.depth);
    rhi_set_mat4(rhi_uniform(p, "u_shadowvp0"), &cascade_vp[0].x.x);
    rhi_set_mat4(rhi_uniform(p, "u_shadowvp1"), &cascade_vp[1].x.x);
    rhi_set_mat4(rhi_uniform(p, "u_shadowvp2"), &cascade_vp[2].x.x);
    rhi_set_f2(rhi_uniform(p, "u_shadowsplit"), cascade_split[0], cascade_split[1]);
    rhi_set_f2(rhi_uniform(p, "u_shadow_texel"), 1.f / (SHADOW_RES * SHADOW_CASCADES), 1.f / SHADOW_RES);
    rhi_set_f3(rhi_uniform(p, "u_shadow_wtexel"), cascade_wtexel[0], cascade_wtexel[1], cascade_wtexel[2]);
}

// ---------------------------------------------------------------------------
// cascades

// Ground footprint of the camera frustum slice between `dnear` and `dfar`, as a bounding sphere.
//
// The corners are built straight from the camera basis (so this is independent of the
// projection's clip-space convention) and then flattened onto the terrain band: this camera
// looks down at a world that lives within a few units of y=0, so fitting the cascade to the raw
// frustum slice would spend almost all of it on empty air and push the ground out of the light's
// range entirely. SHADOW_RELIEF widens the box enough to keep terrain relief inside it.
#define SHADOW_GROUND  2.f       // middle of the world's height band
#define SHADOW_RELIEF 14.f       // world height range, projected into the light's plane
void Lighting::slice_sphere(float dnear, float dfar, vec3 *center, float *radius) {
    float fov = 45.f * 0.0174533f, aspect = screen_w / maxf(1.f, (float)screen_h);
    float th = tanf(fov * 0.5f), tw = th * aspect;
    vec3 corners[8]; int k = 0;
    for (int f = 0; f < 2; ++f) {
        float d = f ? dfar : dnear, hh = d * th, hw = d * tw;
        vec3 c = add3(cam.pos, scale3(cam.fwd, d));
        for (int sy = -1; sy <= 1; sy += 2) for (int sx = -1; sx <= 1; sx += 2) {
            vec3 p = add3(c, add3(scale3(cam.right, hw * sx), scale3(cam.up, hh * sy)));
            corners[k++] = vec3(p.x, SHADOW_GROUND, p.z);
        }
    }
    vec3 mid = vec3(0,0,0);
    for (int i = 0; i < 8; ++i) mid = add3(mid, corners[i]);
    mid = scale3(mid, 1.f/8.f);
    float r = 0;
    for (int i = 0; i < 8; ++i) r = maxf(r, len3(sub3(corners[i], mid)));
    *center = mid; *radius = r + SHADOW_RELIEF;
}

// SHADOW_DEPTH_MARGIN covers casters above/behind the cascade footprint along the light.
// It also sets the depth precision: the ortho depth range is ~2*(radius+margin) world units
// mapped to [0,1], and the lookup bias must stay below the occluder-receiver separation of the
// *shortest* caster. The old value of 260 stretched the range to ~550 units, which pushed the
// bias past the depth separation of boats, houses and walls - they stopped casting entirely
// (issue-2). Everything that casts lives within a few units of the ground band (masts ~3,
// church 0.6, terrain relief already inside SHADOW_RELIEF), so 24 is ample.
#define SHADOW_DEPTH_MARGIN 24.f
mat44 Lighting::cascade_matrix(vec3 center, float radius) {
    vec3 sun = norm3(RF.sun);
    float texels = 2.f * radius / SHADOW_RES;                 // snap the centre to whole texels
    center = vec3(floorf(center.x / texels) * texels, floorf(center.y / texels) * texels, floorf(center.z / texels) * texels);
    float back = radius + SHADOW_DEPTH_MARGIN;
    // The shadow camera sits on the SUN side of the scene looking along -sun (issue-2: this was
    // sub3, placing the eye under the world looking up - depth sorted away from the sun, so an
    // occluder above a receiver stored a *larger* depth and the compare always said "lit";
    // objects never cast onto the water or the ground, only side-facing acne survived).
    vec3 eye = add3(center, scale3(sun, back));
    vec3 up = fabsf(sun.y) > 0.98f ? vec3(0,0,1) : vec3(0,1,0);
    mat44 view = lookat44(eye, center, up);
    mat44 proj = cam_proj_to_gl(mat44_ortho_off_center_rh(-radius, radius, -radius, radius, 0.05f, back + radius + SHADOW_DEPTH_MARGIN));
    return mat44_mul_mat44(view, proj);
}

// MARE_SHADOWDUMP=path.pgm: write the depth atlas once, for diagnosing cascade problems
void Lighting::shadow_dump(const char *path) {
    if (!shadow_atlas.fbo) return;
    int n = shadow_atlas.w * shadow_atlas.h;
    float *d = (float *)malloc(n * sizeof(float));
    rhi_read_depth(&shadow_atlas, d);
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < n; ++i) { if (d[i] < lo) lo = d[i]; if (d[i] < 1.f && d[i] > hi) hi = d[i]; }
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P5\n%d %d\n255\n", shadow_atlas.w, shadow_atlas.h);
        for (int y = shadow_atlas.h - 1; y >= 0; --y) for (int x = 0; x < shadow_atlas.w; ++x) {
            float v = d[y*shadow_atlas.w + x];
            float t = hi > lo ? (v - lo) / (hi - lo) : 0;
            unsigned char b = (unsigned char)(clampf(t, 0, 1) * 255.f);
            fwrite(&b, 1, 1, f);
        }
        fclose(f);
    }
    LOG_INFO("gpu", "shadow dump %s: depth range %.6f..%.6f (cleared=1.0)", path, lo, hi);
    free(d);
}

void Lighting::shadow_render() {
    shadow_casters_drawn = 0;
    if (!shadow_enabled) return;
    if (!shadow_atlas.fbo) {
        shadow_atlas = rhi_target_depth_make(SHADOW_RES * SHADOW_CASCADES, SHADOW_RES);
        LOG_DEBUG("gpu", "shadow atlas %dx%d (%d cascades)", SHADOW_RES * SHADOW_CASCADES, SHADOW_RES, SHADOW_CASCADES);
    }
    // Cascades only need to reach as far as the meshes themselves: past PORT_DETAIL_DIST the
    // towns and ships are replaced by icons, so there is nothing left to cast. Beyond this the
    // lookup returns "lit".
    float zfar = clampf(cam.dist * 2.6f + 70.f, 120.f, 650.f);
    shadow_radius = zfar;
    float znear = cam.znear;
    for (int i = 0; i < SHADOW_CASCADES; ++i) {
        float t0 = i / (float)SHADOW_CASCADES, t1 = (i + 1) / (float)SHADOW_CASCADES;
        // practical split scheme: blend of uniform and logarithmic
        float lin0 = znear + (zfar - znear) * t0, lin1 = znear + (zfar - znear) * t1;
        float log0 = znear * powf(zfar / znear, t0), log1 = znear * powf(zfar / znear, t1);
        float d0 = lerpf(lin0, log0, 0.75f), d1 = lerpf(lin1, log1, 0.75f);
        cascade_split[i] = d1;
        vec3 c; float r; slice_sphere(d0, d1, &c, &r);
        cascade_vp[i] = cascade_matrix(c, r);
        cascade_wtexel[i] = 2.f * r / SHADOW_RES;
    }

    // MARE_SHADOW_DEBUG: log the cascade-0 matrix once so a dumped atlas can be checked offline
    static int logged_mats;
    if (!logged_mats && getenv("MARE_SHADOW_DEBUG")) { logged_mats = 1;
        const float *m = &cascade_vp[0].x.x;
        LOG_INFO("gpu", "cascade0 vp rows: [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] [%g %g %g %g] splits %.1f %.1f",
                 m[0],m[1],m[2],m[3], m[4],m[5],m[6],m[7], m[8],m[9],m[10],m[11], m[12],m[13],m[14],m[15], cascade_split[0], cascade_split[1]);
        for (int i = 0; i < G.nfleets; ++i) { const fleet_t *f = &G.fleets[i];
            if (f->alive && dist2d(f->x, f->z, cam.tx, cam.tz) < 15) { float x, z, h; scene_fleet_pos(f, &x, &z, &h);
                LOG_INFO("gpu", "debug fleet '%s' draws at %.3f %.3f heading %.2f", f->name, x, z, h); break; } }
    }
    rhi_target_clear_depth(&shadow_atlas);
    for (int i = 0; i < SHADOW_CASCADES; ++i) {
        rhi_viewport(i * SHADOW_RES, 0, SHADOW_RES, SHADOW_RES);
        meshes_bind_depth(&cascade_vp[i]);
        shadow_casters_drawn += scene_submit_shadow();
    }
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
vec3     srgb_to_linear3(vec3 c) { return mare::engine::render::lighting().to_linear3(c); }
unsigned rgb_linear(unsigned col) { return mare::engine::render::lighting().rgb_linear(col); }
void render_light_update(void) { mare::engine::render::lighting().light_update(); }
void render_sun_update(float day) { mare::engine::render::lighting().sun_update(day); }
void light_bind(const rhi_pipeline_t *p) { mare::engine::render::lighting().bind(p); }
void shadow_dump(const char *path) { mare::engine::render::lighting().shadow_dump(path); }
void shadow_render(void) { mare::engine::render::lighting().shadow_render(); }
}
