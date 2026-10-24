// OpenGL 3 backend for the RHI (rhi.h). The only game file allowed to call gl*/v3 render
// helpers. Relies on v3's context: compatibility GL on Windows/Linux, 3.2 core on macOS,
// hence the attribute-by-name binding and no layout qualifiers (see glsl.h).

#include <string.h>
#include "rhi.h"
#include "glsl.h"
#include "../core/diag.h"

void rhi_init(void) { /* context is created by v3's app_create; nothing to do on GL */ }
const char *rhi_info(void) {
    static char buf[256];
    snprintf(buf, sizeof buf, "%s | %s", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    return buf;
}
int rhi_check_errors(const char *where) {
    int n = 0; GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR && n < 8) { LOG_ERROR("rhi", "GL error 0x%04x after %s", (unsigned)e, where); n++; }
    return n;
}

void rhi_frame_begin(int w, int h, float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDepthMask(GL_TRUE);   // a pipeline with depth_write 0 may be the last bound: clear needs the mask on
    glClearColor(r, g, b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}

// ---------------------------------------------------------------------------
// render targets

rhi_target_t rhi_target_make(int w, int h, rhi_format_t color_fmt, int with_depth) {
    rhi_target_t t = { .w = w, .h = h };
    t.color = rhi_texture_make(w, h, color_fmt, RHI_FILTER_LINEAR, 0);
    if (with_depth) t.depth = rhi_texture_make(w, h, RHI_FMT_DEPTH24, RHI_FILTER_NEAREST, 0);
    glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color.id, 0);
    if (with_depth) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth.id, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) LOG_FATAL("rhi", "render target %dx%d fmt %d incomplete: 0x%04x", w, h, color_fmt, (unsigned)st);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return t;
}
rhi_target_t rhi_target_depth_make(int w, int h) {
    rhi_target_t t = { .w = w, .h = h };
    t.depth = rhi_texture_make(w, h, RHI_FMT_DEPTH24, RHI_FILTER_NEAREST, 0);
    glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth.id, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);               // depth only: no colour attachment
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) LOG_FATAL("rhi", "depth target %dx%d incomplete: 0x%04x", w, h, (unsigned)st);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return t;
}
void rhi_target_clear_depth(const rhi_target_t *t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}
void rhi_viewport(int x, int y, int w, int h) { glViewport(x, y, w, h); }

void rhi_target_free(rhi_target_t *t) {
    if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
    if (t->color.id) glDeleteTextures(1, &t->color.id);
    if (t->depth.id) glDeleteTextures(1, &t->depth.id);
    memset(t, 0, sizeof *t);
}
void rhi_target_begin(const rhi_target_t *t, float r, float g, float b) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glViewport(0, 0, t->w, t->h);
    glDepthMask(GL_TRUE);
    glClearColor(r, g, b, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
}
void rhi_backbuffer(int w, int h) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
}
void rhi_read_pixels(int x, int y, int w, int h, unsigned char *rgba) {
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
void rhi_read_depth(const rhi_target_t *t, float *depth) {
    glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
    glReadPixels(0, 0, t->w, t->h, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---------------------------------------------------------------------------
// pipelines

rhi_pipeline_t rhi_pipeline_make(const rhi_pipeline_desc_t *d) {
    rhi_pipeline_t p = { .depth_test = d->depth_test, .depth_write = d->depth_write, .blend = d->blend };
    const char *v[2] = { GLSL_VERSION, d->vs }, *f[2] = { GLSL_VERSION, d->fs };
    unsigned vsh = shader('v', 2, v), fsh = shader('f', 2, f);
    p.id = glCreateProgram();
    glAttachShader(p.id, vsh); glAttachShader(p.id, fsh);
    for (int i = 0; i < d->layout->nattrs; ++i) glBindAttribLocation(p.id, i, d->layout->attrs[i].name);
    glLinkProgram(p.id);
    GLint ok; glGetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048]; glGetProgramInfoLog(p.id, sizeof log, 0, log); LOG_FATAL("rhi", "shader link failed: %s", log); }
    glDeleteShader(vsh); glDeleteShader(fsh);
    for (int i = 0; d->uniforms && d->uniforms[i] && p.nuniforms < 24; ++i) { p.names[p.nuniforms] = d->uniforms[i]; p.locs[p.nuniforms] = glGetUniformLocation(p.id, d->uniforms[i]); p.nuniforms++; }
    LOG_DEBUG("rhi", "pipeline %u linked (%d attribs, %d uniforms)", p.id, d->layout->nattrs, p.nuniforms);
    return p;
}

static unsigned rhi_bound_vao;   // avoid redundant binds in per-chunk loops

void rhi_pipeline_bind(const rhi_pipeline_t *p) {
    glUseProgram(p->id);
    rhi_bound_vao = 0;   // v3 (debug draw, microui) may have bound its own VAO between passes
    if (p->depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(p->depth_write ? GL_TRUE : GL_FALSE);
    if (p->blend == RHI_BLEND_ALPHA) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
}

int rhi_uniform(const rhi_pipeline_t *p, const char *name) {
    for (int i = 0; i < p->nuniforms; ++i) if (p->names[i] == name || !strcmp(p->names[i], name)) return p->locs[i];
    return glGetUniformLocation(p->id, name);
}
void rhi_set_f1(int loc, float x)                            { glUniform1f(loc, x); }
void rhi_set_f2(int loc, float x, float y)                   { glUniform2f(loc, x, y); }
void rhi_set_f3(int loc, float x, float y, float z)          { glUniform3f(loc, x, y, z); }
void rhi_set_f4(int loc, float x, float y, float z, float w) { glUniform4f(loc, x, y, z, w); }
void rhi_set_i1(int loc, int x)                              { glUniform1i(loc, x); }
void rhi_set_mat4(int loc, const float *m16)                 { glUniformMatrix4fv(loc, 1, GL_FALSE, m16); }
void rhi_set_f3v(int loc, const float *v, int n)             { glUniform3fv(loc, n, v); }

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

rhi_texture_t rhi_texture_make(int w, int h, rhi_format_t fmt, rhi_filter_t filter, const void *data) {
    rhi_texture_t t = { .w = w, .h = h, .fmt = fmt };
    GLint internal; GLenum format, type; gl_format(fmt, &internal, &format, &type);
    glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter == RHI_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == RHI_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
void rhi_texture_update(rhi_texture_t *t, const void *data) {
    GLint internal; GLenum format, type; gl_format(t->fmt, &internal, &format, &type);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, t->h, format, type, data);
}
void rhi_texture_bind(int slot, const rhi_texture_t *t) {
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

rhi_geometry_t rhi_geometry_make(const rhi_layout_t *layout, const void *verts, int nverts,
                                 const unsigned *indices, int nindices) {
    rhi_geometry_t g = { .nverts = nverts, .nindices = nindices, .inst_stride = layout->stride[1] };
    glGenVertexArrays(1, &g.vao); glBindVertexArray(g.vao);
    glGenBuffers(1, &g.vbo); glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)nverts * layout->stride[0], verts, GL_STATIC_DRAW);
    apply_layout(layout, 0);
    if (indices) {
        glGenBuffers(1, &g.ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, nindices * sizeof(unsigned), indices, GL_STATIC_DRAW);
    }
    if (g.inst_stride) {
        glGenBuffers(1, &g.ibo); glBindBuffer(GL_ARRAY_BUFFER, g.ibo);
        g.inst_capacity = 256; glBufferData(GL_ARRAY_BUFFER, (size_t)g.inst_capacity * g.inst_stride, 0, GL_STREAM_DRAW);
        apply_layout(layout, 1);
    }
    glBindVertexArray(0); rhi_bound_vao = 0;
    return g;
}

void rhi_geometry_instances(rhi_geometry_t *g, const void *data, int n) {
    if (n <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, g->ibo);
    if (n > g->inst_capacity) { g->inst_capacity = n * 2; glBufferData(GL_ARRAY_BUFFER, (size_t)g->inst_capacity * g->inst_stride, 0, GL_STREAM_DRAW); }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)n * g->inst_stride, data);
}

void rhi_draw(rhi_geometry_t *g, int ninstances) {
    if (g->vao != rhi_bound_vao) { glBindVertexArray(g->vao); rhi_bound_vao = g->vao; }
    if (g->nindices) {
        if (ninstances) glDrawElementsInstanced(GL_TRIANGLES, g->nindices, GL_UNSIGNED_INT, 0, ninstances);
        else glDrawElements(GL_TRIANGLES, g->nindices, GL_UNSIGNED_INT, 0);
    } else {
        if (ninstances) glDrawArraysInstanced(GL_TRIANGLES, 0, g->nverts, ninstances);
        else glDrawArrays(GL_TRIANGLES, 0, g->nverts);
    }
}
