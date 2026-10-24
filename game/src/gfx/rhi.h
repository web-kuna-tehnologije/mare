// RHI: the API-agnostic render hardware interface. Every GL call the game makes goes through
// this header; the active backend (rhi_gl.c today, Vulkan/DX12 later) is picked at compile time
// in mare.c. v3's own drawing (debug draw, microui) stays inside the framework.
//
// Design notes for future backends:
// * Pipelines bake in blend/depth/primitive state at creation (no loose state switches).
// * Geometry objects own their vertex/index/instance buffers (a VAO on GL, plain buffer
//   bindings on Vulkan/DX12). Instance data is streamed per frame via rhi_geometry_instances.
// * Uniforms are set by cached location while a pipeline is bound; a non-GL backend maps the
//   same calls onto a per-pipeline uniform block updated at draw time.
// * All vertex attributes are float (matches every mesh in the game).

#ifndef MARE_RHI_H
#define MARE_RHI_H

#include <stddef.h>

typedef enum rhi_format_t {               // texture formats (grows with HDR/sRGB work)
    RHI_FMT_R32F, RHI_FMT_RGB8, RHI_FMT_RGBA8, RHI_FMT_SRGB8, RHI_FMT_SRGB8_A8,
    RHI_FMT_RGBA16F, RHI_FMT_DEPTH24,
} rhi_format_t;
typedef enum rhi_filter_t { RHI_FILTER_NEAREST, RHI_FILTER_LINEAR } rhi_filter_t;
typedef enum rhi_blend_t  { RHI_BLEND_NONE, RHI_BLEND_ALPHA } rhi_blend_t;
typedef enum rhi_usage_t  { RHI_USAGE_STATIC, RHI_USAGE_STREAM } rhi_usage_t;

// opaque handles: 'id' is backend-private, 0 = invalid
typedef struct rhi_texture_t  { unsigned id; int w, h; rhi_format_t fmt; } rhi_texture_t;
typedef struct rhi_pipeline_t {
    unsigned id;                                   // backend program/pipeline object
    int   depth_test, depth_write; rhi_blend_t blend;
    int   nuniforms; const char *names[24]; int locs[24];   // cached uniform locations
} rhi_pipeline_t;

// vertex layout: per-attribute buffer slot (0 = vertex, 1 = instance), float component count,
// byte offset in that buffer's stride, and divisor (0 per-vertex, 1 per-instance)
typedef struct rhi_attr_t   { const char *name; int buffer, components, offset, divisor; } rhi_attr_t;
typedef struct rhi_layout_t { const rhi_attr_t *attrs; int nattrs; int stride[2]; } rhi_layout_t;

typedef struct rhi_pipeline_desc_t {
    const char *vs, *fs;                 // GLSL bodies; the backend prepends its version header
    const rhi_layout_t *layout;          // attribute names bound in declaration order
    const char **uniforms;               // NULL-terminated names to pre-resolve (optional)
    int depth_test, depth_write;         // default off: set explicitly
    rhi_blend_t blend;
} rhi_pipeline_desc_t;

// offscreen render target: one colour attachment + optional depth
typedef struct rhi_target_t { unsigned fbo; rhi_texture_t color, depth; int w, h; } rhi_target_t;

// geometry: vertex buffer (+ optional u32 index buffer, + optional streamed instance buffer)
typedef struct rhi_geometry_t {
    unsigned vao, vbo, ebo, ibo;         // backend-private
    int nverts, nindices, inst_stride, inst_capacity;
} rhi_geometry_t;

// device
void        rhi_init(void);                            // after the window/context exists
const char *rhi_info(void);                            // "<renderer> | <api version>" for logs
int         rhi_check_errors(const char *where);       // logs pending backend errors, returns count

// frame & render targets
void rhi_frame_begin(int w, int h, float r, float g, float b);   // backbuffer: viewport + clear color/depth, depth LEQUAL
rhi_target_t rhi_target_make(int w, int h, rhi_format_t color_fmt, int with_depth);  // fatal if incomplete
rhi_target_t rhi_target_depth_make(int w, int h);                // depth-only target (shadow maps)
void rhi_viewport(int x, int y, int w, int h);                   // sub-rect of the bound target (shadow atlas tiles)
void rhi_target_clear_depth(const rhi_target_t *t);              // bind a depth-only target and clear it
void rhi_target_free(rhi_target_t *t);                           // for resize (recreate after)
void rhi_target_begin(const rhi_target_t *t, float r, float g, float b);  // bind + viewport + clear color/depth
void rhi_backbuffer(int w, int h);                               // bind the window backbuffer, no clear
void rhi_read_pixels(int x, int y, int w, int h, unsigned char *rgba);    // from the bound target (screenshots, smoke tests)
void rhi_read_depth(const rhi_target_t *t, float *depth);                 // whole depth attachment (shadow-map debugging)

// pipelines & uniforms (setters require the pipeline to be bound)
rhi_pipeline_t rhi_pipeline_make(const rhi_pipeline_desc_t *d);  // fatal on compile/link failure
void rhi_pipeline_bind(const rhi_pipeline_t *p);                 // program + its depth/blend state
int  rhi_uniform(const rhi_pipeline_t *p, const char *name);     // cached lookup (-1 if unknown)
void rhi_set_f1(int loc, float x);
void rhi_set_f2(int loc, float x, float y);
void rhi_set_f3(int loc, float x, float y, float z);
void rhi_set_f4(int loc, float x, float y, float z, float w);
void rhi_set_i1(int loc, int x);
void rhi_set_mat4(int loc, const float *m16);
void rhi_set_f3v(int loc, const float *v, int n);

// textures
rhi_texture_t rhi_texture_make(int w, int h, rhi_format_t fmt, rhi_filter_t filter, const void *data);
void rhi_texture_update(rhi_texture_t *t, const void *data);     // full-size upload
void rhi_texture_bind(int slot, const rhi_texture_t *t);

// geometry & draws
rhi_geometry_t rhi_geometry_make(const rhi_layout_t *layout, const void *verts, int nverts,
                                 const unsigned *indices, int nindices);  // indices optional
void rhi_geometry_instances(rhi_geometry_t *g, const void *data, int n); // stream per-frame instance data (grows the buffer)
void rhi_draw(rhi_geometry_t *g, int ninstances);    // indexed if indices exist; ninstances 0 = plain draw

#endif
