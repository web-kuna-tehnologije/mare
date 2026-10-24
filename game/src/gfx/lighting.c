// Directional sun + cascaded shadow maps (DESIGN_RENDERER.md steps 5 and 6).
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

#include <math.h>
#include <stdlib.h>
#include "render.h"
#include "camera.h"
#include "scene.h"
#include "../core/util.h"
#include "../core/diag.h"

#define SHADOW_RES    1024
#define SHADOW_CASCADES 3

int shadow_enabled = 1;
int shadow_casters_drawn;

static rhi_target_t shadow_atlas;
static mat44 cascade_vp[SHADOW_CASCADES];
static float cascade_split[SHADOW_CASCADES];      // view-distance at which each cascade ends
static float cascade_wtexel[SHADOW_CASCADES];     // world size of one shadow texel per cascade

// ---------------------------------------------------------------------------
// colour helpers (CPU side)

static float srgb_to_linear_f(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}
vec3 srgb_to_linear3(vec3 c) { return vec3(srgb_to_linear_f(c.x), srgb_to_linear_f(c.y), srgb_to_linear_f(c.z)); }

// packed 0xAARRGGBB sRGB -> packed linear, for colours handed to v3's debug draw (which writes
// straight into the linear HDR target). 8-bit linear banding is acceptable for overlay lines.
unsigned rgb_linear(unsigned col) {   // colours are 0xAABBGGRR (util.h)
    unsigned a = col & 0xFF000000u;
    unsigned r = (unsigned)(clampf(srgb_to_linear_f(col_r(col)), 0, 1) * 255.f + 0.5f);
    unsigned g = (unsigned)(clampf(srgb_to_linear_f(col_g(col)), 0, 1) * 255.f + 0.5f);
    unsigned b = (unsigned)(clampf(srgb_to_linear_f(col_b(col)), 0, 1) * 255.f + 0.5f);
    return a | (b << 16) | (g << 8) | r;
}

// ---------------------------------------------------------------------------
// the sun

void render_light_update(void) {
    float d = RF.daylight;
    // Sun radiance is tuned so a mid-grey albedo under full sun lands near 0.6 after tonemapping.
    RF.suncol = scale3(srgb_to_linear3(vec3(1.00f, 0.96f, 0.88f)), 3.1f * d);
    // Hemispheric ambient: sky from above, warm bounce from the ground below.
    RF.ambsky = scale3(srgb_to_linear3(RF.sky), 0.55f * d);
    RF.ambgnd = scale3(srgb_to_linear3(vec3(0.33f, 0.30f, 0.25f)), 0.30f * d);
}

void light_bind(const rhi_pipeline_t *p) {
    rhi_set_f3(rhi_uniform(p, "u_sun"), RF.sun.x, RF.sun.y, RF.sun.z);
    rhi_set_f3(rhi_uniform(p, "u_suncol"), RF.suncol.x, RF.suncol.y, RF.suncol.z);
    rhi_set_f3(rhi_uniform(p, "u_ambsky"), RF.ambsky.x, RF.ambsky.y, RF.ambsky.z);
    rhi_set_f3(rhi_uniform(p, "u_ambgnd"), RF.ambgnd.x, RF.ambgnd.y, RF.ambgnd.z);
    int on = shadow_enabled && shadow_atlas.fbo && RF.mapness < 0.98f;
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
static void slice_sphere(float dnear, float dfar, vec3 *center, float *radius) {
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

// SHADOW_DEPTH_MARGIN is deliberately generous: the light must see every caster above the
// cascade's footprint, and anything that falls outside the ortho's depth range is clipped away
// and silently stops casting (or, worse, lands at the near plane and shadows everything).
#define SHADOW_DEPTH_MARGIN 260.f
static mat44 cascade_matrix(vec3 center, float radius) {
    vec3 sun = norm3(RF.sun);
    float texels = 2.f * radius / SHADOW_RES;                 // snap the centre to whole texels
    center = vec3(floorf(center.x / texels) * texels, floorf(center.y / texels) * texels, floorf(center.z / texels) * texels);
    float back = radius + SHADOW_DEPTH_MARGIN;
    vec3 eye = sub3(center, scale3(sun, back));
    vec3 up = fabsf(sun.y) > 0.98f ? vec3(0,0,1) : vec3(0,1,0);
    mat44 view = lookat44(eye, center, up);
    mat44 proj = mat44_ortho_off_center_rh(-radius, radius, -radius, radius, 0.05f, back + radius + SHADOW_DEPTH_MARGIN);
    return mat44_mul_mat44(view, proj);
}

// MARE_SHADOWDUMP=path.pgm: write the depth atlas once, for diagnosing cascade problems
void shadow_dump(const char *path) {
    if (!shadow_atlas.fbo) return;
    int n = shadow_atlas.w * shadow_atlas.h;
    float *d = malloc(n * sizeof(float));
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

void shadow_render(void) {
    shadow_casters_drawn = 0;
    if (!shadow_enabled) return;
    if (!shadow_atlas.fbo) {
        shadow_atlas = rhi_target_depth_make(SHADOW_RES * SHADOW_CASCADES, SHADOW_RES);
        LOG_DEBUG("gpu", "shadow atlas %dx%d (%d cascades)", SHADOW_RES * SHADOW_CASCADES, SHADOW_RES, SHADOW_CASCADES);
    }
    // Cascades only need to reach as far as the meshes themselves: past PORT_DETAIL_DIST the
    // towns and ships are replaced by icons, so there is nothing left to cast. Beyond this the
    // lookup returns "lit".
    float far = clampf(cam.dist * 1.2f + 40.f, 50.f, 300.f);
    float near = cam.znear;
    for (int i = 0; i < SHADOW_CASCADES; ++i) {
        float t0 = i / (float)SHADOW_CASCADES, t1 = (i + 1) / (float)SHADOW_CASCADES;
        // practical split scheme: blend of uniform and logarithmic
        float lin0 = near + (far - near) * t0, lin1 = near + (far - near) * t1;
        float log0 = near * powf(far / near, t0), log1 = near * powf(far / near, t1);
        float d0 = lerpf(lin0, log0, 0.75f), d1 = lerpf(lin1, log1, 0.75f);
        cascade_split[i] = d1;
        vec3 c; float r; slice_sphere(d0, d1, &c, &r);
        cascade_vp[i] = cascade_matrix(c, r);
        cascade_wtexel[i] = 2.f * r / SHADOW_RES;
    }

    rhi_target_clear_depth(&shadow_atlas);
    for (int i = 0; i < SHADOW_CASCADES; ++i) {
        rhi_viewport(i * SHADOW_RES, 0, SHADOW_RES, SHADOW_RES);
        meshes_bind_depth(&cascade_vp[i]);
        shadow_casters_drawn += scene_submit_shadow();
    }
}
