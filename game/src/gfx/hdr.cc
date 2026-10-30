// mare::engine::render::Hdr (see render.h): HDR: the 3D scene renders into an RGBA16F offscreen
// target; resolve() tonemaps it to the
// backbuffer with a fullscreen triangle. The tonemap is identity below 1.0 and a hue-preserving
// soft rolloff above, so SDR-authored content looks unchanged while shaders may now emit > 1
// (sun specular, foam highlights). Exposure is a plain multiplier (dashboard-adjustable).
// The proper linear/sRGB pipeline is the next step (docs/DESIGN_RENDERER.md).

#include "../platform/v3cpp.hh"
#include <cstdlib>

extern "C" {
#include "render.h"
#include "glsl.h"
#include "camera.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../engine/shaders.h"
}

float hdr_exposure = 1.f;

// Screen-space ambient occlusion (DESIGN_VISUAL.md step 9), at half resolution from the HDR
// depth. World position comes back through the inverse view-projection, the normal from its
// screen derivatives; eight hemisphere samples in a world-space radius that grows a little with
// distance are projected back and compared against the depth there, with a range check so a
// distant object behind a near one does not occlude it. Applied in the tonemap, so it darkens
// the whole pixel rather than the ambient alone - the compromise of a post pass; the terrain's
// baked horizon occlusion (terrain.c) is the one that touches ambient only.
float hdr_ssao_strength = 0.7f; int hdr_ssao_enabled = 1;

// Bloom (DESIGN_VISUAL.md step 10): a quarter-resolution bright pass and a separable blur,
// added back in the tonemap. The scene is HDR, so the sun disc, its glow and the sun's glitter
// on the water are the things above the threshold - which is what makes the sun read as a
// light source rather than a white circle.
float hdr_bloom = 0.55f; int hdr_bloom_enabled = 1;

namespace mare::engine::render {

class Hdr {
public:
    void init();
    void bloom_pass();
    void ao_pass();
    void begin(int w, int h, float r, float g, float b);
    void capture_scene();
    const rhi_texture_t *scene_copy_ptr();     // hdr_scene_copy (the member keeps the C name)
    const rhi_texture_t *depth();
    void resolve(int w, int h);

private:
    rhi_target_t hdr_target;
    rhi_pipeline_t tonemap_prg;
    rhi_geometry_t fs_tri;
    rhi_target_t ao_target; rhi_pipeline_t ao_prg;
    rhi_target_t bloom_a, bloom_b; rhi_pipeline_t bright_prg, blur_prg;
    rhi_texture_t scene_copy;
};

Hdr &hdr() { static Hdr h; return h; }

void Hdr::init() {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    static const char *uniforms[] = { "u_scene", "u_exposure", "u_ao", "u_ao_mix", "u_ao_texel", "u_bloom", "u_bloom_mix",
                                      "u_depth", "u_inv_vp", "u_campos", "u_sun", "u_suncol", "u_vfog", "u_vfogcol", 0 };
    const rhi_pipeline_desc_t tonemap_desc{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("hdr/tonemap.frag"), .layout = &layout, .uniforms = uniforms };   // (compound literals -> named: C++ cannot take &T{...})
    tonemap_prg = rhi_pipeline_make(&tonemap_desc);
    static const char *ao_uniforms[] = { "u_depth", "u_inv_vp", "u_vp", "u_campos", "u_texel", "u_on", 0 };
    const rhi_pipeline_desc_t ao_desc{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("hdr/ao.frag"), .layout = &layout, .uniforms = ao_uniforms };
    ao_prg = rhi_pipeline_make(&ao_desc);
    static const char *bright_uniforms[] = { "u_scene", "u_threshold", "u_exposure", 0 };
    const rhi_pipeline_desc_t bright_desc{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("hdr/bright.frag"), .layout = &layout, .uniforms = bright_uniforms };
    bright_prg = rhi_pipeline_make(&bright_desc);
    static const char *blur_uniforms[] = { "u_src", "u_dir", 0 };
    const rhi_pipeline_desc_t blur_desc{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("hdr/blur.frag"), .layout = &layout, .uniforms = blur_uniforms };
    blur_prg = rhi_pipeline_make(&blur_desc);
    { const char *b = getenv("MARE_BLOOM"); if (b) { hdr_bloom = clampf((float)atof(b), 0.f, 2.f); hdr_bloom_enabled = hdr_bloom > 0.f; LOG_INFO("gpu", "bloom %.2f (MARE_BLOOM)", hdr_bloom); } }
    float tri[] = { -1,-1, 3,-1, -1,3 };
    fs_tri = rhi_geometry_make(&layout, tri, 3, 0, 0);
    const char *e = getenv("MARE_SSAO"); if (e) { hdr_ssao_strength = clampf((float)atof(e), 0.f, 1.f); hdr_ssao_enabled = hdr_ssao_strength > 0.f; LOG_INFO("gpu", "SSAO strength %.2f (MARE_SSAO)", hdr_ssao_strength); }
}

// bright pass + separable blur at quarter resolution, before the tonemap reads it
void Hdr::bloom_pass() {
    if (!hdr_target.fbo || !hdr_bloom_enabled) return;
    int w = hdr_target.w / 4, h = hdr_target.h / 4;
    if (bloom_a.w != w || bloom_a.h != h) {
        if (bloom_a.fbo) rhi_target_free(&bloom_a);
        if (bloom_b.fbo) rhi_target_free(&bloom_b);
        bloom_a = rhi_target_make(w, h, RHI_FMT_RGBA16F, 0);
        bloom_b = rhi_target_make(w, h, RHI_FMT_RGBA16F, 0);
    }
    rhi_target_begin(&bloom_a, 0, 0, 0);
    rhi_pipeline_bind(&bright_prg);
    rhi_set_i1(UNI(bright_prg, "u_scene"), 0); rhi_texture_bind(0, &hdr_target.color);
    rhi_set_f1(UNI(bright_prg, "u_threshold"), 1.05f);
    rhi_set_f1(UNI(bright_prg, "u_exposure"), hdr_exposure);
    rhi_draw(&fs_tri, 0);
    for (int pass = 0; pass < 2; ++pass) {           // horizontal, then vertical
        rhi_target_begin(pass ? &bloom_a : &bloom_b, 0, 0, 0);
        rhi_pipeline_bind(&blur_prg);
        rhi_set_i1(UNI(blur_prg, "u_src"), 0); rhi_texture_bind(0, pass ? &bloom_b.color : &bloom_a.color);
        rhi_set_f2(UNI(blur_prg, "u_dir"), pass ? 0.f : 1.f / w, pass ? 1.f / h : 0.f);
        rhi_draw(&fs_tri, 0);
    }
}

void Hdr::ao_pass() {
    if (!hdr_target.fbo) return;
    int w = hdr_target.w / 2, h = hdr_target.h / 2;
    if (ao_target.w != w || ao_target.h != h) { if (ao_target.fbo) rhi_target_free(&ao_target); ao_target = rhi_target_make(w, h, RHI_FMT_R32F, 0); }
    rhi_target_begin(&ao_target, 1, 1, 1);
    rhi_pipeline_bind(&ao_prg);
    rhi_set_i1(UNI(ao_prg, "u_depth"), 0); rhi_texture_bind(0, &hdr_target.depth);
    rhi_set_mat4(UNI(ao_prg, "u_inv_vp"), &cam.inv_vp.x.x); rhi_set_mat4(UNI(ao_prg, "u_vp"), &cam.vp.x.x);
    rhi_set_f3(UNI(ao_prg, "u_campos"), cam.pos.x, cam.pos.y, cam.pos.z);
    rhi_set_f2(UNI(ao_prg, "u_texel"), 1.f / w, 1.f / h);
    rhi_set_f1(UNI(ao_prg, "u_on"), hdr_ssao_enabled && RF.mapness < 0.5f ? 1.f : 0.f);
    rhi_draw(&fs_tri, 0);
    // Back to the scene: the AO pass now runs *before* the water (mare.c), so whatever follows
    // must draw into the HDR target again, not into this half-resolution buffer.
    rhi_target_resume(&hdr_target);
}

// bind the HDR target for the 3D passes (recreated on resize); clears to the sky colour
void Hdr::begin(int w, int h, float r, float g, float b) {
    if (hdr_target.w != w || hdr_target.h != h) {
        if (hdr_target.fbo) rhi_target_free(&hdr_target);
        hdr_target = rhi_target_make(w, h, RHI_FMT_RGBA16F, 1);
        LOG_DEBUG("gpu", "HDR target %dx%d (RGBA16F)", w, h);
    }
    rhi_target_begin(&hdr_target, r, g, b);
}

// The scene as drawn so far (terrain, buildings, ships), copied for the water to refract
// (DESIGN_VISUAL.md step 5 follow-up). Called once per frame just before sea_draw.
void Hdr::capture_scene() { rhi_target_snapshot(&hdr_target, &scene_copy); }
const rhi_texture_t *Hdr::scene_copy_ptr() { return scene_copy.id ? &scene_copy : NULL; }
const rhi_texture_t *Hdr::depth() { return hdr_target.depth.id ? &hdr_target.depth : NULL; }

// tonemap the HDR scene to the backbuffer; UI draws on top afterwards
void Hdr::resolve(int w, int h) {
    rhi_backbuffer(w, h);
    rhi_pipeline_bind(&tonemap_prg);
    rhi_set_i1(UNI(tonemap_prg, "u_scene"), 0);
    rhi_texture_bind(0, &hdr_target.color);
    rhi_set_f1(UNI(tonemap_prg, "u_exposure"), hdr_exposure);
    { int on = hdr_bloom_enabled && bloom_a.fbo && RF.mapness < 0.9f;
      rhi_set_i1(UNI(tonemap_prg, "u_bloom"), 2); rhi_texture_bind(2, on ? &bloom_a.color : &hdr_target.color);
      rhi_set_f1(UNI(tonemap_prg, "u_bloom_mix"), on ? hdr_bloom : 0.f); }
    {   // volumetric fog: weather decides how much there is (clear air leaves it off)
      float dens = RF.vfog_density, on3 = dens > 0.0001f && RF.mapness < 0.8f;
      rhi_set_i1(UNI(tonemap_prg, "u_depth"), 3); rhi_texture_bind(3, &hdr_target.depth);
      rhi_set_mat4(UNI(tonemap_prg, "u_inv_vp"), &cam.inv_vp.x.x);
      rhi_set_f3(UNI(tonemap_prg, "u_campos"), cam.pos.x, cam.pos.y, cam.pos.z);
      rhi_set_f3(UNI(tonemap_prg, "u_sun"), RF.sun.x, RF.sun.y, RF.sun.z);
      rhi_set_f3(UNI(tonemap_prg, "u_suncol"), RF.suncol.x, RF.suncol.y, RF.suncol.z);
      rhi_set_f4(UNI(tonemap_prg, "u_vfog"), on3 ? dens : 0.f, RF.vfog_height, RF.vfog_sun, 2200.f);
      vec3 fc = srgb_to_linear3(RF.vfog_color);
      rhi_set_f3(UNI(tonemap_prg, "u_vfogcol"), fc.x, fc.y, fc.z); }
    { int on = hdr_ssao_enabled && ao_target.fbo && RF.mapness < 0.5f;
      rhi_set_i1(UNI(tonemap_prg, "u_ao"), 1); rhi_texture_bind(1, on ? &ao_target.color : &hdr_target.color);
      rhi_set_f1(UNI(tonemap_prg, "u_ao_mix"), on ? hdr_ssao_strength * clampf(1.f - RF.mapness * 2.f, 0.f, 1.f) : 0.f);
      rhi_set_f2(UNI(tonemap_prg, "u_ao_texel"), ao_target.w ? 1.f / ao_target.w : 0.f, ao_target.h ? 1.f / ao_target.h : 0.f); }
    rhi_draw(&fs_tri, 0);
}

} // namespace mare::engine::render

// C ABI (render.h) for the unported callers
extern "C" {
void hdr_init(void) { mare::engine::render::hdr().init(); }
void hdr_bloom_pass(void) { mare::engine::render::hdr().bloom_pass(); }
void hdr_ao_pass(void) { mare::engine::render::hdr().ao_pass(); }
void hdr_begin(int w, int h, float r, float g, float b) { mare::engine::render::hdr().begin(w, h, r, g, b); }
void hdr_capture_scene(void) { mare::engine::render::hdr().capture_scene(); }
const rhi_texture_t *hdr_scene_copy(void) { return mare::engine::render::hdr().scene_copy_ptr(); }
const rhi_texture_t *hdr_depth(void) { return mare::engine::render::hdr().depth(); }
void hdr_resolve(int w, int h) { mare::engine::render::hdr().resolve(w, h); }
}
