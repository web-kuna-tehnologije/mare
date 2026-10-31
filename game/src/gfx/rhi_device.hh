// mare::engine::rhi::Device - the abstract render device the C ABI in rhi.h dispatches to
// (docs/REWRITE_PLAN.md T4). Every backend implements this one interface:
//
//   GlDevice      rhi_gl.cc    the production backend (GL 3 / 4.0 tessellation / 3.2 geometry)
//   VulkanDevice  rhi_vk.cc    bring-up: loads the loader, creates instance + device + queue,
//                              reports the adapter; drawing is delegated to the GL device
//                              until the SPIR-V pipeline work lands (T8)
//   Dx12Device    rhi_dx12.cc  the API-neutrality stub: reports itself and delegates
//
// The active device is chosen once, before rhi_init (rhi_select / --rhi= / MARE_RHI), and the
// `rhi_*` C functions in rhi.cc are one-line shims onto it, so no pass knows which backend it
// draws through. The handles (rhi_texture_t, rhi_pipeline_t, ...) stay the plain structs of
// rhi.h: a backend owns what their `id`/`vao`/`fbo` fields mean.

#ifndef MARE_RHI_DEVICE_HH
#define MARE_RHI_DEVICE_HH

extern "C" {
#include "rhi.h"
}

namespace mare::engine::rhi {

struct Device {
    virtual ~Device() {}
    virtual const char *backend_name() = 0;

    // device
    virtual void        init() = 0;
    virtual const char *info() = 0;
    virtual int         check_errors(const char *where) = 0;

    // frame & targets
    virtual void         frame_begin(int w, int h, float r, float g, float b) = 0;
    virtual rhi_mem_t    memory() = 0;
    virtual rhi_target_t target_make(int w, int h, rhi_format_t color_fmt, int with_depth) = 0;
    virtual rhi_target_t target_depth_make(int w, int h) = 0;
    virtual void         target_clear_depth(const rhi_target_t *t) = 0;
    virtual void         viewport(int x, int y, int w, int h) = 0;
    virtual void         target_snapshot(const rhi_target_t *t, rhi_texture_t *dst) = 0;
    virtual void         target_snapshot_depth(const rhi_target_t *t, rhi_texture_t *dst) = 0;
    virtual void         target_free(rhi_target_t *t) = 0;
    virtual void         target_begin(const rhi_target_t *t, float r, float g, float b) = 0;
    virtual void         target_resume(const rhi_target_t *t) = 0;
    virtual void         backbuffer(int w, int h) = 0;
    virtual void         read_pixels(int x, int y, int w, int h, unsigned char *rgba) = 0;
    virtual void         read_depth(const rhi_target_t *t, float *depth) = 0;

    // pipelines & uniforms
    virtual int            tessellation_supported() = 0;
    virtual int            geometry_shader_supported() = 0;
    virtual rhi_pipeline_t pipeline_make(const rhi_pipeline_desc_t *d) = 0;
    virtual void           pipeline_bind(const rhi_pipeline_t *p) = 0;
    virtual int            uniform(const rhi_pipeline_t *p, const char *name) = 0;
    virtual void set_f1(int loc, float x) = 0;
    virtual void set_f2(int loc, float x, float y) = 0;
    virtual void set_f3(int loc, float x, float y, float z) = 0;
    virtual void set_f4(int loc, float x, float y, float z, float w) = 0;
    virtual void set_i1(int loc, int x) = 0;
    virtual void set_mat4(int loc, const float *m16) = 0;
    virtual void set_f3v(int loc, const float *v, int n) = 0;
    virtual void set_f4v(int loc, const float *v, int n) = 0;
    virtual void set_f1v(int loc, const float *v, int n) = 0;

    // textures
    virtual rhi_texture_t texture_make(int w, int h, rhi_format_t fmt, rhi_filter_t filter, rhi_wrap_t wrap, const void *data) = 0;
    virtual void          texture_update(rhi_texture_t *t, const void *data) = 0;
    virtual void          texture_update_rect(rhi_texture_t *t, int x, int y, int w, int h, const void *data) = 0;
    virtual void          texture_bind(int slot, const rhi_texture_t *t) = 0;

    // geometry & draws
    virtual rhi_geometry_t geometry_make(const rhi_layout_t *layout, const void *verts, int nverts, const unsigned *indices, int nindices) = 0;
    virtual void           geometry_free(rhi_geometry_t *g) = 0;
    virtual void           geometry_update(rhi_geometry_t *g, const void *verts, int nverts) = 0;
    virtual void           geometry_instances(rhi_geometry_t *g, const void *data, int n) = 0;
    virtual void           draw(rhi_geometry_t *g, int ninstances) = 0;
    virtual void           frame_stats_reset() = 0;
    virtual rhi_stats_t    frame_stats() = 0;

    // GPU timers
    virtual int         gpu_timers_supported() = 0;
    virtual int         gpu_scope(const char *name) = 0;
    virtual const char *gpu_scope_name(int s) = 0;
    virtual int         gpu_scope_count() = 0;
    virtual float       gpu_ms(int s) = 0;
    virtual void        gpu_begin(int s) = 0;
    virtual void        gpu_end() = 0;
    virtual void        gpu_collect() = 0;

    // occlusion queries
    virtual int      occlusion_supported() = 0;
    virtual unsigned query_make() = 0;
    virtual void     query_begin(unsigned q) = 0;
    virtual void     query_end() = 0;
    virtual int      query_result(unsigned q, int *passed) = 0;
    virtual void     color_write(int on) = 0;

    // anisotropy
    virtual float anisotropy() = 0;
    virtual float anisotropy_max() = 0;
    virtual void  set_anisotropy(float level) = 0;
};

Device &gl_device();                 // always available: the bring-up backends draw through it
Device &vulkan_device();             // rhi_vk.cc
Device &dx12_device();               // rhi_dx12.cc
Device &active();                    // the selected backend (GL until rhi_select says otherwise)
void    select(const char *name);    // "gl" | "vulkan" | "dx12"; unknown names log and keep GL
const char *backend_name();

// A bring-up backend: opens its own API for the report, delegates every call to the GL device.
// Shared by the Vulkan and DX12 devices so the interface stays the single point of contact.
struct DelegatingDevice : Device {
    Device &gl() { return gl_device(); }
    void         frame_begin(int w, int h, float r, float g, float b) override { gl().frame_begin(w, h, r, g, b); }
    rhi_mem_t    memory() override { return gl().memory(); }
    rhi_target_t target_make(int w, int h, rhi_format_t f, int d) override { return gl().target_make(w, h, f, d); }
    rhi_target_t target_depth_make(int w, int h) override { return gl().target_depth_make(w, h); }
    void         target_clear_depth(const rhi_target_t *t) override { gl().target_clear_depth(t); }
    void         viewport(int x, int y, int w, int h) override { gl().viewport(x, y, w, h); }
    void         target_snapshot(const rhi_target_t *t, rhi_texture_t *dst) override { gl().target_snapshot(t, dst); }
    void         target_snapshot_depth(const rhi_target_t *t, rhi_texture_t *dst) override { gl().target_snapshot_depth(t, dst); }
    void         target_free(rhi_target_t *t) override { gl().target_free(t); }
    void         target_begin(const rhi_target_t *t, float r, float g, float b) override { gl().target_begin(t, r, g, b); }
    void         target_resume(const rhi_target_t *t) override { gl().target_resume(t); }
    void         backbuffer(int w, int h) override { gl().backbuffer(w, h); }
    void         read_pixels(int x, int y, int w, int h, unsigned char *rgba) override { gl().read_pixels(x, y, w, h, rgba); }
    void         read_depth(const rhi_target_t *t, float *depth) override { gl().read_depth(t, depth); }
    int            tessellation_supported() override { return gl().tessellation_supported(); }
    int            geometry_shader_supported() override { return gl().geometry_shader_supported(); }
    rhi_pipeline_t pipeline_make(const rhi_pipeline_desc_t *d) override { return gl().pipeline_make(d); }
    void           pipeline_bind(const rhi_pipeline_t *p) override { gl().pipeline_bind(p); }
    int            uniform(const rhi_pipeline_t *p, const char *name) override { return gl().uniform(p, name); }
    void set_f1(int loc, float x) override { gl().set_f1(loc, x); }
    void set_f2(int loc, float x, float y) override { gl().set_f2(loc, x, y); }
    void set_f3(int loc, float x, float y, float z) override { gl().set_f3(loc, x, y, z); }
    void set_f4(int loc, float x, float y, float z, float w) override { gl().set_f4(loc, x, y, z, w); }
    void set_i1(int loc, int x) override { gl().set_i1(loc, x); }
    void set_mat4(int loc, const float *m16) override { gl().set_mat4(loc, m16); }
    void set_f3v(int loc, const float *v, int n) override { gl().set_f3v(loc, v, n); }
    void set_f4v(int loc, const float *v, int n) override { gl().set_f4v(loc, v, n); }
    void set_f1v(int loc, const float *v, int n) override { gl().set_f1v(loc, v, n); }
    rhi_texture_t texture_make(int w, int h, rhi_format_t fmt, rhi_filter_t filter, rhi_wrap_t wrap, const void *data) override { return gl().texture_make(w, h, fmt, filter, wrap, data); }
    void          texture_update(rhi_texture_t *t, const void *data) override { gl().texture_update(t, data); }
    void          texture_update_rect(rhi_texture_t *t, int x, int y, int w, int h, const void *d) override { gl().texture_update_rect(t, x, y, w, h, d); }
    void          texture_bind(int slot, const rhi_texture_t *t) override { gl().texture_bind(slot, t); }
    rhi_geometry_t geometry_make(const rhi_layout_t *l, const void *v, int nv, const unsigned *i, int ni) override { return gl().geometry_make(l, v, nv, i, ni); }
    void           geometry_free(rhi_geometry_t *g) override { gl().geometry_free(g); }
    void           geometry_update(rhi_geometry_t *g, const void *v, int n) override { gl().geometry_update(g, v, n); }
    void           geometry_instances(rhi_geometry_t *g, const void *d, int n) override { gl().geometry_instances(g, d, n); }
    void           draw(rhi_geometry_t *g, int n) override { gl().draw(g, n); }
    void           frame_stats_reset() override { gl().frame_stats_reset(); }
    rhi_stats_t    frame_stats() override { return gl().frame_stats(); }
    int         gpu_timers_supported() override { return gl().gpu_timers_supported(); }
    int         gpu_scope(const char *name) override { return gl().gpu_scope(name); }
    const char *gpu_scope_name(int s) override { return gl().gpu_scope_name(s); }
    int         gpu_scope_count() override { return gl().gpu_scope_count(); }
    float       gpu_ms(int s) override { return gl().gpu_ms(s); }
    void        gpu_begin(int s) override { gl().gpu_begin(s); }
    void        gpu_end() override { gl().gpu_end(); }
    void        gpu_collect() override { gl().gpu_collect(); }
    int      occlusion_supported() override { return gl().occlusion_supported(); }
    unsigned query_make() override { return gl().query_make(); }
    void     query_begin(unsigned q) override { gl().query_begin(q); }
    void     query_end() override { gl().query_end(); }
    int      query_result(unsigned q, int *passed) override { return gl().query_result(q, passed); }
    void     color_write(int on) override { gl().color_write(on); }
    float anisotropy() override { return gl().anisotropy(); }
    float anisotropy_max() override { return gl().anisotropy_max(); }
    void  set_anisotropy(float level) override { gl().set_anisotropy(level); }
};

} // namespace mare::engine::rhi

#endif
