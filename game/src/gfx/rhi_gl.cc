// mare::engine::render::GlDevice - the OpenGL 3 backend for the RHI (rhi.h). The only game
// file allowed to call gl*/v3 render helpers. Relies on v3's context: compatibility GL on
// Windows/Linux, 3.2 core on macOS, hence the attribute-by-name binding and no layout
// qualifiers (see glsl.h). The C ABI in rhi.h shims onto the device singleton; the Vulkan and
// DX12 backends of the rewrite plan (T4/T8) will join it behind an abstract rhi::Device.

#include "../platform/v3cpp.hh"

#include <cstring>
#include <cstdlib>

#include "rhi_device.hh"

extern "C" {
#include "glsl.h"
#include "../core/diag.h"
#include "../core/util.h"
}

namespace mare::engine::render {

// Anisotropic filtering (DESIGN_VISUAL.md step 2). Everything in this game is seen at a
// grazing angle: the sea stretches to the horizon, roofs and roads lie almost edge-on under a
// 12-28 degree camera. Trilinear filtering picks one mip level per pixel from the *largest*
// axis of the footprint, so those surfaces take a far blurrier mip than they need and the sea's
// ripples smear into grey a few units out. Anisotropy samples along the footprint instead.
// The extension is in core GL since 4.6 and present as EXT_texture_filter_anisotropic on
// everything older that matters; when it is missing the textures are plain trilinear.
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif
#define RHI_ANISO_TRACK 128
#define RHI_GPU_LAG 4
struct gpu_scope_t { const char *name; unsigned q[RHI_GPU_LAG]; int issued[RHI_GPU_LAG]; float ms; };

class GlDevice : public rhi::Device {
public:
    const char *backend_name() override { return "gl"; }
    // --- the whole former file-scope state, same names so the bodies read unchanged
    float rhi_aniso_max = 0, rhi_aniso = 0;    // driver maximum, and what we ask for
    // Mipmapped textures made so far, so the dashboard knob can re-apply the level to textures
    // that already exist (the parameter is per texture object, set at creation).
    unsigned rhi_aniso_tex[RHI_ANISO_TRACK] = {}; int rhi_aniso_ntex = 0;
    rhi_mem_t rhi_mem = {};
    unsigned rhi_bound_vao = 0;      // avoid redundant binds in per-chunk loops
    int rhi_bound_patches = 0;       // the bound pipeline tessellates: draw issues GL_PATCHES
    int rhi_bound_lines = 0;         // the bound pipeline takes a line list (a geometry stage expands it)
    rhi_stats_t rhi_stats = {};
    gpu_scope_t gpu_scopes[RHI_GPU_SCOPES] = {};
    int gpu_nscopes = 0, gpu_frame = 0, gpu_open = -1, gpu_supported = -1;
    int occq_target = 0;

    float anisotropy() { return rhi_aniso; }
    float anisotropy_max() { return rhi_aniso_max; }
    void set_anisotropy(float level) {
    if (rhi_aniso_max < 2.f) return;
    rhi_aniso = clampf(level, 1.f, rhi_aniso_max);
    for (int i = 0; i < rhi_aniso_ntex; ++i) {
        glBindTexture(GL_TEXTURE_2D, rhi_aniso_tex[i]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, rhi_aniso);
    }
}

void init() {
    // context is created by v3's app_create; query what the driver offers
    while (glGetError()) {}
    GLfloat m = 0; glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m);
    if (glGetError() || m < 2.f) { rhi_aniso_max = rhi_aniso = 1.f; LOG_WARN("gpu", "anisotropic filtering unsupported: textures stay trilinear"); return; }
    rhi_aniso_max = m; rhi_aniso = m;
    const char *e = getenv("MARE_ANISO");
    if (e) { rhi_aniso = clampf((float)atof(e), 1.f, m); LOG_INFO("gpu", "anisotropic filtering pinned to %.0fx of %.0fx (MARE_ANISO)", rhi_aniso, m); }
    else LOG_INFO("gpu", "anisotropic filtering %.0fx (driver maximum)", rhi_aniso);
}
const char *info() {
    static char buf[256];
    snprintf(buf, sizeof buf, "%s | %s", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    return buf;
}
int check_errors(const char *where) {
    int n = 0; GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR && n < 8) { LOG_ERROR("rhi", "GL error 0x%04x after %s", (unsigned)e, where); n++; }
    return n;
}

void frame_begin(int w, int h, float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDepthMask(GL_TRUE);   // a pipeline with depth_write 0 may be the last bound: clear needs the mask on
    glClearColor(r, g, b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}

// ---------------------------------------------------------------------------
// GPU memory accounting (what we allocated, not what the driver committed)

rhi_mem_t memory() { return rhi_mem; }

static size_t format_bpp(rhi_format_t f) {
    switch (f) {
    case RHI_FMT_R32F: return 4;
    case RHI_FMT_RGB8: case RHI_FMT_SRGB8: return 3;
    case RHI_FMT_RGBA8: case RHI_FMT_SRGB8_A8: case RHI_FMT_DEPTH24: return 4;
    case RHI_FMT_RGBA16F: return 8;
    default: return 4;
    }
}

// ---------------------------------------------------------------------------
// render targets

// render-target images are accounted under `targets`, not `textures`: rhi_texture_make has
// already counted them, so move the bytes across rather than counting them twice
void mem_reclassify_as_target(size_t bytes, int ntex) {
    rhi_mem.textures -= bytes; rhi_mem.ntextures -= ntex;
    rhi_mem.targets  += bytes;
}

rhi_target_t target_make(int w, int h, rhi_format_t color_fmt, int with_depth) {
    rhi_target_t t = { .w = w, .h = h };
    t.color = texture_make(w, h, color_fmt, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
    if (with_depth) t.depth = texture_make(w, h, RHI_FMT_DEPTH24, RHI_FILTER_NEAREST, RHI_WRAP_CLAMP, 0);
    glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color.id, 0);
    if (with_depth) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth.id, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) LOG_FATAL("rhi", "render target %dx%d fmt %d incomplete: 0x%04x", w, h, color_fmt, (unsigned)st);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mem_reclassify_as_target((size_t)w * h * (format_bpp(color_fmt) + (with_depth ? 4 : 0)), with_depth ? 2 : 1);
    return t;
}
rhi_target_t target_depth_make(int w, int h) {
    rhi_target_t t = { .w = w, .h = h };
    // LINEAR + compare mode = hardware PCF: each tap of the shadow lookup is a bilinear-filtered
    // depth comparison (sampler2DShadow), so shadow edges are smooth instead of stair-stepped
    t.depth = texture_make(w, h, RHI_FMT_DEPTH24, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
    glBindTexture(GL_TEXTURE_2D, t.depth.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth.id, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);               // depth only: no colour attachment
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) LOG_FATAL("rhi", "depth target %dx%d incomplete: 0x%04x", w, h, (unsigned)st);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mem_reclassify_as_target((size_t)w * h * 4, 1);
    return t;
}
void target_clear_depth(const rhi_target_t *t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}
void viewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }
void target_snapshot(const rhi_target_t *t, rhi_texture_t *dst) {
    if (dst->id && (dst->w != t->w || dst->h != t->h)) { glDeleteTextures(1, &dst->id); rhi_mem.textures -= (size_t)dst->w * dst->h * format_bpp(dst->fmt); rhi_mem.ntextures--; dst->id = 0; }
    if (!dst->id) *dst = texture_make(t->w, t->h, t->color.fmt, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
    glActiveTexture(GL_TEXTURE0 + 7); glBindTexture(GL_TEXTURE_2D, dst->id);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, t->w, t->h);
}
// The target's depth, copied the same way (0.22.4). The water used to sample the *live* depth
// attachment it was writing into - a feedback loop that handed it its own freshly written
// depth from the patches drawn before, so the refraction thickness read as zero in tiles and
// along patch borders and the staged battle's sea was ruled into sky-coloured blocks.
void target_snapshot_depth(const rhi_target_t *t, rhi_texture_t *dst) {
    if (!t->depth.id) return;
    if (dst->id && (dst->w != t->w || dst->h != t->h)) { glDeleteTextures(1, &dst->id); rhi_mem.textures -= (size_t)dst->w * dst->h * format_bpp(dst->fmt); rhi_mem.ntextures--; dst->id = 0; }
    if (!dst->id) *dst = texture_make(t->w, t->h, RHI_FMT_DEPTH24, RHI_FILTER_LINEAR, RHI_WRAP_CLAMP, 0);
    glActiveTexture(GL_TEXTURE0 + 7); glBindTexture(GL_TEXTURE_2D, dst->id);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, t->w, t->h);
}

void target_free(rhi_target_t *t) {
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->color.id) { glDeleteTextures(1, &t->color.id); rhi_mem.targets -= (size_t)t->color.w * t->color.h * format_bpp(t->color.fmt); }
    if (t->depth.id) { glDeleteTextures(1, &t->depth.id); rhi_mem.targets -= (size_t)t->depth.w * t->depth.h * format_bpp(t->depth.fmt); }
    memset(t, 0, sizeof *t);
}
void target_begin(const rhi_target_t *t, float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glDepthMask(GL_TRUE);
    glClearColor(r, g, b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}
void target_resume(const rhi_target_t *t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}
void backbuffer(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
}
void read_pixels(int x, int y, int w, int h, unsigned char *rgba) {
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void read_depth(const rhi_target_t *t, float *depth) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glReadPixels(0, 0, t->w, t->h, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// pipelines

#ifndef GL_TESS_CONTROL_SHADER
#define GL_TESS_CONTROL_SHADER 0x8E88
#define GL_TESS_EVALUATION_SHADER 0x8E87
#define GL_PATCH_VERTICES 0x8E72
#define GL_PATCHES 0x000E
#endif
int tessellation_supported() {
    static int sup = -1;
    if (sup < 0) {
        const char *e = getenv("MARE_TESS");
        sup = GLAD_GL_VERSION_4_0 && glPatchParameteri && glCreateShader ? 1 : 0;
        if (e && !atoi(e)) sup = 0;
        LOG_INFO("gpu", "tessellation %s%s", sup ? "available (GL 4.0)" : "unavailable", e && !atoi(e) ? " (disabled by MARE_TESS=0)" : "");
    }
    return sup;
}
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif
// Geometry stage: core since GL 3.2. MARE_GEOM=0 forces the vertex-only fallbacks (the rig's
// cordage goes back to fixed-width quads), which is the A/B for the screen-space ropes.
int geometry_shader_supported() {
    static int sup = -1;
    if (sup < 0) {
        const char *e = getenv("MARE_GEOM");
        sup = (GLAD_GL_VERSION_3_2 || GLAD_GL_VERSION_4_0) && glCreateShader ? 1 : 0;
        if (e && !atoi(e)) sup = 0;
        LOG_INFO("gpu", "geometry stage %s%s", sup ? "available (GL 3.2)" : "unavailable", e && !atoi(e) ? " (disabled by MARE_GEOM=0)" : "");
    }
    return sup;
}
void set_f1(int loc, float x)                            { glUniform1f(loc, x); }
void set_f2(int loc, float x, float y)                   { glUniform2f(loc, x, y); }
void set_f3(int loc, float x, float y, float z)          { glUniform3f(loc, x, y, z); }
void set_f4(int loc, float x, float y, float z, float w) { glUniform4f(loc, x, y, z, w); }
void set_i1(int loc, int x)                              { glUniform1i(loc, x); }
void set_mat4(int loc, const float *m16)                 { glUniformMatrix4fv(loc, 1, GL_FALSE, m16); }
void set_f3v(int loc, const float *v, int n)             { glUniform3fv(loc, n, v); }
void set_f4v(int loc, const float *v, int n)             { glUniform4fv(loc, n, v); }
void set_f1v(int loc, const float *v, int n)             { glUniform1fv(loc, n, v); }
// the framework's shader() helper knows vertex/fragment/geometry only; tessellation stages
// compile here, with the same source dump on failure
static unsigned stage(GLenum type, int n, const char **src, const char *what) {
    LOG_DEBUG("rhi", "compiling %s stage (%d chars)", what, (int)strlen(src[n-1]));
    GLuint id = glCreateShader(type);
    if (!id) LOG_FATAL("rhi", "glCreateShader(%s) returned 0: GL error 0x%04x", what, (unsigned)glGetError());
    glShaderSource(id, n, src, NULL); glCompileShader(id);
    GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048]; glGetShaderInfoLog(id, sizeof log, NULL, log); LOG_FATAL("rhi", "%s shader failed: %s", what, log); }
    return id;
}

rhi_pipeline_t pipeline_make(const rhi_pipeline_desc_t *d) {
    rhi_pipeline_t p = { .depth_test = d->depth_test, .depth_write = d->depth_write, .blend = d->blend };
    p.lines = d->lines;
    int tess = d->tcs && d->tes, geom = d->gs != NULL;
    // every stage of one program takes the same version header; tessellation needs 4.00, a
    // geometry stage 1.50 (GL 3.2)
    const char *ver = tess ? "#version 400\n" : geom ? "#version 150\n" : GLSL_VERSION;
    const char *v[2] = { ver, d->vs }, *f[2] = { ver, d->fs };
    unsigned vsh = tess || geom ? stage(GL_VERTEX_SHADER, 2, v, "vertex") : shader('v', 2, v);
    unsigned fsh = tess || geom ? stage(GL_FRAGMENT_SHADER, 2, f, "fragment") : shader('f', 2, f);
    p.id = glCreateProgram();
    glAttachShader(p.id, vsh); glAttachShader(p.id, fsh);
    unsigned csh = 0, esh = 0, gsh = 0;
    if (geom) {
        const char *g[2] = { ver, d->gs };
        gsh = stage(GL_GEOMETRY_SHADER, 2, g, "geometry");
        glAttachShader(p.id, gsh);
    }
    if (tess) {
        const char *c[2] = { ver, d->tcs }, *e[2] = { ver, d->tes };
        csh = stage(GL_TESS_CONTROL_SHADER, 2, c, "tess control"); esh = stage(GL_TESS_EVALUATION_SHADER, 2, e, "tess evaluation");
        glAttachShader(p.id, csh); glAttachShader(p.id, esh);
        p.patches = d->patch_verts > 0 ? d->patch_verts : 4;
        LOG_DEBUG("rhi", "tessellation stages attached, linking");
    }
    for (int i = 0; i < d->layout->nattrs; ++i) glBindAttribLocation(p.id, i, d->layout->attrs[i].name);
    glLinkProgram(p.id);
    GLint ok; glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p.id, sizeof log, 0, log); LOG_FATAL("rhi", "shader link failed: %s", log); }
    glDeleteShader(vsh); glDeleteShader(fsh);
    if (csh) glDeleteShader(csh); if (esh) glDeleteShader(esh); if (gsh) glDeleteShader(gsh);
    for (int i = 0; d->uniforms && d->uniforms[i] && p.nuniforms < 24; ++i) { p.names[p.nuniforms] = d->uniforms[i]; p.locs[p.nuniforms] = glGetUniformLocation(p.id, d->uniforms[i]); p.nuniforms++; }
    LOG_DEBUG("rhi", "pipeline %u linked (%d attribs, %d uniforms)", p.id, d->layout->nattrs, p.nuniforms);
    return p;
}

void frame_stats_reset() { memset(&rhi_stats, 0, sizeof rhi_stats); }
rhi_stats_t frame_stats() { return rhi_stats; }

// ---------------------------------------------------------------------------
// GPU timer queries (ARB_timer_query, core since GL 3.3)
//
// Each scope keeps a small ring of queries. We write into the slot for this frame and read back
// the slot RHI_GPU_LAG frames old, which is always finished by then, so the CPU never stalls.

int gpu_timers_ok() {
    if (gpu_supported < 0) {
        gpu_supported = glGenQueries && glBeginQuery && glEndQuery && glGetQueryObjectui64v ? 1 : 0;
        if (!gpu_supported) LOG_WARN("rhi", "GPU timer queries unavailable: per-pass GPU times will read 0");
    }
    return gpu_supported;
}
int gpu_timers_supported() { return gpu_timers_ok(); }

int gpu_scope(const char *name) {
    for (int i = 0; i < gpu_nscopes; ++i) if (!strcmp(gpu_scopes[i].name, name)) return i;
    if (gpu_nscopes >= RHI_GPU_SCOPES) return -1;
    gpu_scopes[gpu_nscopes].name = name;
    return gpu_nscopes++;
}
const char *gpu_scope_name(int s) { return s >= 0 && s < gpu_nscopes ? gpu_scopes[s].name : ""; }
int gpu_scope_count() { return gpu_nscopes; }
float gpu_ms(int s) { return s >= 0 && s < gpu_nscopes ? gpu_scopes[s].ms : 0.f; }

void gpu_begin(int s) {
    if (s < 0 || !gpu_timers_ok() || gpu_open >= 0) return;
    gpu_scope_t *g = &gpu_scopes[s];
    int slot = gpu_frame % RHI_GPU_LAG;
    if (!g->q[slot]) glGenQueries(1, &g->q[slot]);
    glBeginQuery(GL_TIME_ELAPSED, g->q[slot]);
    g->issued[slot] = 1; gpu_open = s;
}
void gpu_end() {
    if (gpu_open < 0) return;
    glEndQuery(GL_TIME_ELAPSED);
    gpu_open = -1;
}
void gpu_collect() {
    if (!gpu_timers_ok()) return;
    int slot = (gpu_frame + 1) % RHI_GPU_LAG;          // the oldest slot in the ring
    for (int i = 0; i < gpu_nscopes; ++i) {
        gpu_scope_t *g = &gpu_scopes[i];
        if (!g->issued[slot] || !g->q[slot]) continue;
        GLint ready = 0; glGetQueryObjectiv(g->q[slot], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (!ready) continue;
        GLuint64 ns = 0; glGetQueryObjectui64v(g->q[slot], GL_QUERY_RESULT, &ns);
        g->ms = (float)(ns / 1.0e6); g->issued[slot] = 0;
    }
    gpu_frame++;
}

void pipeline_bind(const rhi_pipeline_t *p) {
    glUseProgram(p->id);
    rhi_bound_vao = 0;   // v3 (debug draw, microui) may have bound its own VAO between passes
    rhi_bound_patches = p->patches;
    rhi_bound_lines = p->lines;
    if (p->patches && glPatchParameteri) glPatchParameteri(GL_PATCH_VERTICES, p->patches);
    if (p->depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(p->depth_write ? GL_TRUE : GL_FALSE);
    if (p->blend == RHI_BLEND_ALPHA) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
}

int uniform(const rhi_pipeline_t *p, const char *name) {
    for (int i = 0; i < p->nuniforms; ++i) if (p->names[i] == name || !strcmp(p->names[i], name)) return p->locs[i];
    return glGetUniformLocation(p->id, name);
}
// ---------------------------------------------------------------------------
// textures

static void gl_format(rhi_format_t fmt, GLint *internal, GLenum *format, GLenum *type) {
    switch (fmt) {
    case RHI_FMT_R32F:     *internal = GL_R32F;              *format = GL_RED;  *type = GL_FLOAT; break;
    case RHI_FMT_RGB8:     *internal = GL_RGB8;              *format = GL_RGB;  *type = GL_UNSIGNED_BYTE; break;
    case RHI_FMT_RGBA8:    *internal = GL_RGBA8;             *format = GL_RGBA; *type = GL_UNSIGNED_BYTE; break;
    case RHI_FMT_SRGB8:    *internal = GL_SRGB8;             *format = GL_RGB;  *type = GL_UNSIGNED_BYTE; break;
    case RHI_FMT_SRGB8_A8: *internal = GL_SRGB8_ALPHA8;      *format = GL_RGBA; *type = GL_UNSIGNED_BYTE; break;
    case RHI_FMT_RGBA16F:  *internal = GL_RGBA16F;           *format = GL_RGBA; *type = GL_HALF_FLOAT; break;
    case RHI_FMT_DEPTH24:  *internal = GL_DEPTH_COMPONENT24; *format = GL_DEPTH_COMPONENT; *type = GL_UNSIGNED_INT; break;
    default: LOG_FATAL("rhi", "unknown texture format %d", fmt);
    }
}

rhi_texture_t texture_make(int w, int h, rhi_format_t fmt, rhi_filter_t filter, rhi_wrap_t wrap, const void *data) {
    rhi_texture_t t = { .w = w, .h = h, .fmt = fmt };
    GLint internal; GLenum format, type; gl_format(fmt, &internal, &format, &type);
    GLint wr = wrap == RHI_WRAP_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    rhi_mem.textures += (size_t)w * h * format_bpp(fmt); rhi_mem.ntextures++;
    glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    filter == RHI_FILTER_MIPMAP ? GL_LINEAR_MIPMAP_LINEAR : filter == RHI_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == RHI_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wr);
    if (filter == RHI_FILTER_MIPMAP) {
        glGenerateMipmap(GL_TEXTURE_2D); rhi_mem.textures += (size_t)w * h * format_bpp(fmt) / 3;
        // only mipmapped textures benefit: anisotropy is about which mip a grazing pixel takes
        if (rhi_aniso_max > 1.f) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, rhi_aniso);
            if (rhi_aniso_ntex < RHI_ANISO_TRACK) rhi_aniso_tex[rhi_aniso_ntex++] = t.id;
        }
    }
    return t;
}
void texture_update(rhi_texture_t *t, const void *data) {
    GLint internal; GLenum format, type; gl_format(t->fmt, &internal, &format, &type);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, t->h, format, type, data);
}
void texture_update_rect(rhi_texture_t *t, int x, int y, int w, int h, const void *data) {
    GLint internal; GLenum format, type; gl_format(t->fmt, &internal, &format, &type);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, data);
}
void texture_bind(int slot, const rhi_texture_t *t) {
    glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_2D, t->id);
}

// ---------------------------------------------------------------------------
// geometry

static void apply_layout(const rhi_layout_t *l, int buffer) {
    for (int i = 0; i < l->nattrs; ++i) {
        const rhi_attr_t *a = &l->attrs[i]; if (a->buffer != buffer) continue;
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, a->components, GL_FLOAT, 0, l->stride[buffer], (void*)(size_t)a->offset);
        if (a->divisor) glVertexAttribDivisor(i, a->divisor);
    }
}

rhi_geometry_t geometry_make(const rhi_layout_t *layout, const void *verts, int nverts,
                             const unsigned *indices, int nindices) {
    rhi_geometry_t g = { .nverts = nverts, .nindices = nindices, .inst_stride = layout->stride[1] };
    glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)nverts * layout->stride[0], verts, GL_STATIC_DRAW);
    rhi_mem.buffers += (size_t)nverts * layout->stride[0]; rhi_mem.nbuffers++;
    apply_layout(layout, 0);
    if (indices) {
        glGenBuffers(1, &g.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, nindices * sizeof(unsigned), indices, GL_STATIC_DRAW);
        rhi_mem.buffers += (size_t)nindices * sizeof(unsigned); rhi_mem.nbuffers++;
    }
    if (g.inst_stride) {
        glGenBuffers(1, &g.ibo); glBindBuffer(GL_ARRAY_BUFFER, g.ibo);
        g.inst_capacity = 256; glBufferData(GL_ARRAY_BUFFER, (size_t)g.inst_capacity * g.inst_stride, 0, GL_STREAM_DRAW);
        rhi_mem.buffers += (size_t)g.inst_capacity * g.inst_stride; rhi_mem.nbuffers++;
        apply_layout(layout, 1);
    }
    glBindVertexArray(0); rhi_bound_vao = 0;
    g.vstride = layout->stride[0];
    return g;
}

// Release a geometry's buffers (LOD streaming: gfx/scene.c evicts meshes it has stopped
// drawing). The accounting mirrors rhi_geometry_make so rhi_memory() stays truthful.
void geometry_free(rhi_geometry_t *g) {
    if (!g || !g->vao) return;
    if (rhi_bound_vao == g->vao) { glBindVertexArray(0); rhi_bound_vao = 0; }
    if (g->vbo) { glDeleteBuffers(1, &g->vbo); rhi_mem.buffers -= (size_t)g->nverts * g->vstride; rhi_mem.nbuffers--; }
    if (g->ebo) { glDeleteBuffers(1, &g->ebo); rhi_mem.buffers -= (size_t)g->nindices * sizeof(unsigned); rhi_mem.nbuffers--; }
    if (g->ibo) { glDeleteBuffers(1, &g->ibo); rhi_mem.buffers -= (size_t)g->inst_capacity * g->inst_stride; rhi_mem.nbuffers--; }
    glDeleteVertexArrays(1, &g->vao);
    memset(g, 0, sizeof *g);
}

// Dynamic vertices (a simulated flag rebuilt every frame): orphan the buffer, refill it, and
// grow it when the vertex count outruns the allocation. Accounting stays truthful.
void geometry_update(rhi_geometry_t *g, const void *verts, int nverts) {
    if (!g || !g->vbo || nverts <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
    if (nverts > g->nverts) {
        rhi_mem.buffers -= (size_t)g->nverts * g->vstride;
        glBufferData(GL_ARRAY_BUFFER, (size_t)nverts * g->vstride, verts, GL_STREAM_DRAW);
        rhi_mem.buffers += (size_t)nverts * g->vstride;
    } else {
        glBufferData(GL_ARRAY_BUFFER, (size_t)g->nverts * g->vstride, 0, GL_STREAM_DRAW);   // orphan: no stall on the frame in flight
        glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)nverts * g->vstride, verts);
    }
    g->nverts = nverts;
    rhi_stats.upload_bytes += (size_t)nverts * g->vstride;
}

void geometry_instances(rhi_geometry_t *g, const void *data, int n) {
    if (n <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, g->ibo);
    if (n > g->inst_capacity) {
        rhi_mem.buffers -= (size_t)g->inst_capacity * g->inst_stride;
        g->inst_capacity = n * 2; glBufferData(GL_ARRAY_BUFFER, (size_t)g->inst_capacity * g->inst_stride, 0, GL_STREAM_DRAW);
        rhi_mem.buffers += (size_t)g->inst_capacity * g->inst_stride;
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)n * g->inst_stride, data);
    rhi_stats.upload_bytes += (size_t)n * g->inst_stride;
}

// ---------------------------------------------------------------------------
// occlusion queries (rhi.h): GL 3.3 boolean queries; the conservative variant (GL 4.3) is used
// when the loader has its token, since a proxy box only needs "might be visible".
#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif
#ifndef GL_ANY_SAMPLES_PASSED_CONSERVATIVE
#define GL_ANY_SAMPLES_PASSED_CONSERVATIVE 0x8D6A
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
int occlusion_supported() { return glGenQueries && glBeginQuery && glEndQuery && glGetQueryObjectui64v ? 1 : 0; }
unsigned query_make() { unsigned q = 0; if (occlusion_supported()) glGenQueries(1, &q); return q; }
void query_begin(unsigned q) {
    if (!q) return;
    if (!occq_target) {   // probe once: GL 4.3 drivers accept the conservative target, older ones error
        while (glGetError()) {}
        glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE, q);
        if (glGetError()) { occq_target = GL_ANY_SAMPLES_PASSED; glBeginQuery(occq_target, q); } else { occq_target = GL_ANY_SAMPLES_PASSED_CONSERVATIVE; }
        return;
    }
    glBeginQuery(occq_target, q);
}
void query_end() { if (occq_target) glEndQuery(occq_target); }
int query_result(unsigned q, int *passed) {
    if (!q) { *passed = 1; return 1; }
    GLuint64 avail = 0; glGetQueryObjectui64v(q, GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) return 0;
    GLuint64 r = 1; glGetQueryObjectui64v(q, GL_QUERY_RESULT, &r);
    *passed = r != 0; return 1;
}
void color_write(int on) { glColorMask(on, on, on, on); }

void draw(rhi_geometry_t *g, int ninstances) {
    if (g->vao != rhi_bound_vao) { glBindVertexArray(g->vao); rhi_bound_vao = g->vao; }
    rhi_stats.draws++; rhi_stats.instances += ninstances ? ninstances : 1;
    rhi_stats.tris += (g->nindices ? g->nindices : g->nverts) / (rhi_bound_lines ? 1 : 3) * (ninstances ? ninstances : 1);   // patches: the tessellator decides; counted as control points / 3; a line becomes one quad (2 tris)
    GLenum mode = rhi_bound_patches ? GL_PATCHES : rhi_bound_lines ? GL_LINES : GL_TRIANGLES;
    if (g->nindices) {
        if (ninstances) glDrawElementsInstanced(mode, g->nindices, GL_UNSIGNED_INT, 0, ninstances);
        else glDrawElements(mode, g->nindices, GL_UNSIGNED_INT, 0);
    } else {
        if (ninstances) glDrawArraysInstanced(mode, 0, g->nverts, ninstances);
        else glDrawArrays(mode, 0, g->nverts);
    }
}

};  // class GlDevice

GlDevice &gl_device_impl() { static GlDevice d; return d; }

} // namespace mare::engine::render

// the abstract-device accessor (rhi_device.hh); the C ABI shims live in rhi.cc
namespace mare::engine::rhi { Device &gl_device() { return mare::engine::render::gl_device_impl(); } }
