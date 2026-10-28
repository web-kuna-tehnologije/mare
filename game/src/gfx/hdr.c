// HDR: the 3D scene renders into an RGBA16F offscreen target; hdr_resolve() tonemaps it to the
// backbuffer with a fullscreen triangle. The tonemap is identity below 1.0 and a hue-preserving
// soft rolloff above, so SDR-authored content looks unchanged while shaders may now emit > 1
// (sun specular, foam highlights). Exposure is a plain multiplier (dashboard-adjustable).
// The proper linear/sRGB pipeline is the next step (docs/DESIGN_RENDERER.md).

#include <stdlib.h>
#include "render.h"
#include "glsl.h"
#include "camera.h"
#include "../core/util.h"
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
    "uniform sampler2D u_scene; uniform float u_exposure; uniform sampler2D u_ao; uniform float u_ao_mix; uniform vec2 u_ao_texel;\n"
    "out vec4 fragColor;\n"
    GLSL_COLOR
    "void main(){\n"
    "  vec3 c = texture(u_scene, v_uv).rgb * u_exposure;\n"
    // ambient occlusion (hdr_ao_pass, half resolution): four bilinear taps blur the noise
    "  if (u_ao_mix > 0.001) {\n"
    "    float ao = (texture(u_ao, v_uv + u_ao_texel * vec2(-0.5, -0.5)).r + texture(u_ao, v_uv + u_ao_texel * vec2(0.5, -0.5)).r\n"
    "              + texture(u_ao, v_uv + u_ao_texel * vec2(-0.5, 0.5)).r + texture(u_ao, v_uv + u_ao_texel * vec2(0.5, 0.5)).r) * 0.25;\n"
    "    c *= mix(1.0, ao, u_ao_mix);\n"
    "  }\n"
    // Reinhard-style shoulder on luminance: keeps midtones, rolls highlights off smoothly
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  const float W = 3.0;  float lt = l * (1.0 + l / (W * W)) / (1.0 + l);\n"
    "  c *= l > 1e-5 ? lt / l : 1.0;\n"
    "  fragColor = vec4(linear_to_srgb(c), 1.0);\n"   // the only place linear becomes sRGB
    "}\n";

// Screen-space ambient occlusion (DESIGN_VISUAL.md step 9), at half resolution from the HDR
// depth. World position comes back through the inverse view-projection, the normal from its
// screen derivatives; eight hemisphere samples in a world-space radius that grows a little with
// distance are projected back and compared against the depth there, with a range check so a
// distant object behind a near one does not occlude it. Applied in the tonemap, so it darkens
// the whole pixel rather than the ambient alone - the compromise of a post pass; the terrain's
// baked horizon occlusion (terrain.c) is the one that touches ambient only.
static rhi_target_t ao_target; static rhi_pipeline_t ao_prg;
float hdr_ssao_strength = 0.7f; int hdr_ssao_enabled = 1;
static const char *ao_fs =
    "in vec2 v_uv; out vec4 fragColor;\n"
    "uniform sampler2D u_depth; uniform mat4 u_inv_vp; uniform mat4 u_vp; uniform vec3 u_campos; uniform vec2 u_texel; uniform float u_on;\n"
    "vec3 wpos_at(vec2 uv, float z) { vec4 w = u_inv_vp * vec4(uv * 2.0 - 1.0, z * 2.0 - 1.0, 1.0); return w.xyz / w.w; }\n"
    "float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n"
    "void main(){\n"
    "  float z = texture(u_depth, v_uv).r;\n"
    "  if (u_on < 0.5 || z >= 0.99999) { fragColor = vec4(1.0); return; }\n"
    "  vec3 P = wpos_at(v_uv, z);\n"
    "  vec3 N = normalize(cross(dFdx(P), dFdy(P)));\n"
    "  if (dot(N, u_campos - P) < 0.0) N = -N;\n"
    "  float dist = distance(P, u_campos);\n"
    "  if (dist > 220.0) { fragColor = vec4(1.0); return; }\n"
    "  float R = 0.28 + 0.022 * dist;\n"
    "  float rot = hash(v_uv * 1000.0) * 6.2831853;\n"
    "  float occ = 0.0;\n"
    "  for (int i = 0; i < 8; ++i) {\n"
    "    float a = rot + float(i) * 0.7853982; float e = 0.35 + 0.6 * fract(float(i) * 0.618034 + hash(v_uv * 300.0 + float(i)));\n"
    "    vec3 s = vec3(cos(a) * e, 0.0, sin(a) * e);\n"
    "    vec3 up = abs(N.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);\n"
    "    vec3 tx = normalize(cross(up, N)), tz = cross(N, tx);\n"
    "    vec3 dir = tx * s.x + tz * s.z + N * (0.25 + 0.5 * e);\n"
    "    vec3 Q = P + dir * R * (0.3 + 0.7 * float(i + 1) / 8.0);\n"
    "    vec4 c = u_vp * vec4(Q, 1.0); vec2 uv2 = c.xy / c.w * 0.5 + 0.5;\n"
    "    if (uv2.x < 0.0 || uv2.x > 1.0 || uv2.y < 0.0 || uv2.y > 1.0) continue;\n"
    "    float z2 = texture(u_depth, uv2).r; vec3 Pq = wpos_at(uv2, z2);\n"
    "    float dq = distance(Pq, u_campos), dQ = distance(Q, u_campos);\n"
    "    float range = smoothstep(0.0, 1.0, R / max(abs(dq - dQ), 1e-4));\n"
    "    occ += (dq < dQ - 0.03 * R ? 1.0 : 0.0) * range;\n"
    "  }\n"
    "  fragColor = vec4(vec3(1.0 - occ / 8.0), 1.0);\n"
    "}\n";

void hdr_init(void) {
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2*sizeof(float), 0 } };
    static const char *uniforms[] = { "u_scene", "u_exposure", "u_ao", "u_ao_mix", "u_ao_texel", 0 };
    tonemap_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = tonemap_vs, .fs = tonemap_fs, .layout = &layout, .uniforms = uniforms });
    static const char *ao_uniforms[] = { "u_depth", "u_inv_vp", "u_vp", "u_campos", "u_texel", "u_on", 0 };
    ao_prg = rhi_pipeline_make(&(rhi_pipeline_desc_t){ .vs = tonemap_vs, .fs = ao_fs, .layout = &layout, .uniforms = ao_uniforms });
    float tri[] = { -1,-1, 3,-1, -1,3 };
    fs_tri = rhi_geometry_make(&layout, tri, 3, 0, 0);
    const char *e = getenv("MARE_SSAO"); if (e) { hdr_ssao_strength = clampf((float)atof(e), 0.f, 1.f); hdr_ssao_enabled = hdr_ssao_strength > 0.f; LOG_INFO("gpu", "SSAO strength %.2f (MARE_SSAO)", hdr_ssao_strength); }
}

void hdr_ao_pass(void) {
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

// The scene as drawn so far (terrain, buildings, ships), copied for the water to refract
// (DESIGN_VISUAL.md step 5 follow-up). Called once per frame just before sea_draw.
static rhi_texture_t scene_copy;
void hdr_capture_scene(void) { rhi_target_snapshot(&hdr_target, &scene_copy); }
const rhi_texture_t *hdr_scene_copy(void) { return scene_copy.id ? &scene_copy : NULL; }
const rhi_texture_t *hdr_depth(void) { return hdr_target.depth.id ? &hdr_target.depth : NULL; }

// tonemap the HDR scene to the backbuffer; UI draws on top afterwards
void hdr_resolve(int w, int h) {
    rhi_backbuffer(w, h);
    rhi_pipeline_bind(&tonemap_prg);
    rhi_set_i1(UNI(tonemap_prg, "u_scene"), 0);
    rhi_texture_bind(0, &hdr_target.color);
    rhi_set_f1(UNI(tonemap_prg, "u_exposure"), hdr_exposure);
    { int on = hdr_ssao_enabled && ao_target.fbo && RF.mapness < 0.5f;
      rhi_set_i1(UNI(tonemap_prg, "u_ao"), 1); rhi_texture_bind(1, on ? &ao_target.color : &hdr_target.color);
      rhi_set_f1(UNI(tonemap_prg, "u_ao_mix"), on ? hdr_ssao_strength * clampf(1.f - RF.mapness * 2.f, 0.f, 1.f) : 0.f);
      rhi_set_f2(UNI(tonemap_prg, "u_ao_texel"), ao_target.w ? 1.f / ao_target.w : 0.f, ao_target.h ? 1.f / ao_target.h : 0.f); }
    rhi_draw(&fs_tri, 0);
}
