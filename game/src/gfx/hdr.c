// HDR: the 3D scene renders into an RGBA16F offscreen target; hdr_resolve() tonemaps it to the
// backbuffer with a fullscreen triangle. The tonemap is identity below 1.0 and a hue-preserving
// soft rolloff above, so SDR-authored content looks unchanged while shaders may now emit > 1
// (sun specular, foam highlights). Exposure is a plain multiplier (dashboard-adjustable).
// The proper linear/sRGB pipeline is the next step (docs/DESIGN_RENDERER.md).

#include "render.h"
#include "glsl.h"

#include "../core/diag.h"

float hdr_exposure = 1.f;
static rhi_target_t hdr_target;
static rhi_pipeline_t tonemap_prg;
static rhi_geometry_t fs_tri;

static const char *tonemap_vs =
    "in vec2 a_pos;\n"
    "out vec2 v_uv;\n"
    "void main(){ v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *tonemap_fs =
    "in vec2 v_uv;\n"
    "uniform sampler2D u_scene; uniform float u_exposure;\n"
    "out vec4 fragColor;\n"
    GLSL_COLOR
    "void main(){\n"
    "  vec3 c = texture(u_scene, v_uv).rgb * u_exposure;\n"
    // Reinhard-style shoulder on luminance: keeps midtones, rolls highlights off smoothly
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  const float W = 3.0;  float lt = l * (1.0 + l / (W * W)) / (1.0 + l);\n"
    "  c *= l > 1e-5 ? lt / l : 1.0;\n"
    "  fragColor = vec4(linear_to_srgb(c), 1.0);\n"   // the only place linear becomes sRGB
    "}\n";

void hdr_init(void) {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    static const char *uniforms[] = { "u_scene", "u_exposure", 0 };
    tonemap_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = tonemap_vs, .fs = tonemap_fs, .layout = &layout, .uniforms = uniforms });
    float tri[] = { -1,-1, 3,-1, -1,3 };
    fs_tri = rhi_geometry_make(&layout, tri, 3, 0, 0);
}

// bind the HDR target for the 3D passes (recreated on resize); clears to the sky colour
void hdr_begin(int w, int h, float r, float g, float b) {
    if (hdr_target.w != w || hdr_target.h != h) {
        if (hdr_target.fbo) rhi_target_free(&hdr_target);
        hdr_target = rhi_target_make(w, h, RHI_FMT_RGBA16F, 1);
        LOG_DEBUG("gpu", "HDR target %dx%d (RGBA16F)", w, h);
    }
    rhi_target_begin(&hdr_target, r, g, b);
}

// tonemap the HDR scene to the backbuffer; UI draws on top afterwards
void hdr_resolve(int w, int h) {
    rhi_backbuffer(w, h);
    rhi_pipeline_bind(&tonemap_prg);
    rhi_set_i1(UNI(tonemap_prg, "u_scene"), 0);
    rhi_texture_bind(0, &hdr_target.color);
    rhi_set_f1(UNI(tonemap_prg, "u_exposure"), hdr_exposure);
    rhi_draw(&fs_tri, 0);
}
