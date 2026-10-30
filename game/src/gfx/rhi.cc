// The RHI's C ABI (rhi.h) and backend selection (docs/REWRITE_PLAN.md T4): every rhi_* call
// is a one-line shim onto the active mare::engine::rhi::Device. The device is picked once,
// before rhi_init - by --rhi=, MARE_RHI or the default "gl" - and never changes afterwards:
// the handles a pass holds belong to the backend that made them.

#include "rhi_device.hh"

#include <cstring>
#include <cstdlib>
#include <cstdio>

extern "C" {
#include "../core/diag.h"
}

namespace mare::engine::rhi {

// Which backend the game draws through. One object owns the choice and the name, so the
// selection state is not loose file statics: the C shims below reach it through the singleton
// exactly as every other module's shims do (docs/CPP_CONVERSION.md).
class BackendSelector {
public:
    static BackendSelector &instance() { static BackendSelector b; return b; }

    void select(const char *name) {
        if (!name || !*name) return;
        Device *d = nullptr; const char *picked = nullptr;
        if      (!strcmp(name, "gl")     || !strcmp(name, "opengl")) { d = &gl_device();     picked = "gl"; }
        else if (!strcmp(name, "vulkan") || !strcmp(name, "vk"))     { d = &vulkan_device(); picked = "vulkan"; }
        else if (!strcmp(name, "dx12")   || !strcmp(name, "d3d12"))  { d = &dx12_device();   picked = "dx12"; }
        else { LOG_WARN("rhi", "unknown render backend '%s' (gl | vulkan | dx12): staying on GL", name); return; }
        active_ = d; snprintf(name_, sizeof name_, "%s", picked);
        LOG_INFO("rhi", "render backend: %s", name_);
    }

    // The first call decides, so a pass that draws before main() has selected still gets a
    // device; MARE_RHI is read here rather than at static-init time, when the log is not up.
    Device &active() {
        if (!active_) {
            if (const char *e = getenv("MARE_RHI")) select(e);
            if (!active_) active_ = &gl_device();
        }
        return *active_;
    }
    const char *name() const { return name_; }

private:
    Device *active_ = nullptr;
    char    name_[16] = "gl";
};

void        select(const char *name) { BackendSelector::instance().select(name); }
Device     &active() { return BackendSelector::instance().active(); }
const char *backend_name() { return BackendSelector::instance().name(); }

} // namespace mare::engine::rhi

// ---------------------------------------------------------------------------
// C ABI (rhi.h): the passes call the device through these

using mare::engine::rhi::active;

extern "C" {
void        rhi_select(const char *backend) { mare::engine::rhi::select(backend); }
const char *rhi_backend(void) { return mare::engine::rhi::backend_name(); }
float rhi_anisotropy(void) { return active().anisotropy(); }
float rhi_anisotropy_max(void) { return active().anisotropy_max(); }
void rhi_set_anisotropy(float level) { active().set_anisotropy(level); }
void rhi_init(void) { active().init(); }
const char *rhi_info(void) { return active().info(); }
int rhi_check_errors(const char *where) { return active().check_errors(where); }
void rhi_frame_begin(int w, int h, float r, float g, float b) { active().frame_begin(w, h, r, g, b); }
rhi_mem_t rhi_memory(void) { return active().memory(); }
rhi_target_t rhi_target_make(int w, int h, rhi_format_t color_fmt, int with_depth) { return active().target_make(w, h, color_fmt, with_depth); }
rhi_target_t rhi_target_depth_make(int w, int h) { return active().target_depth_make(w, h); }
void rhi_target_clear_depth(const rhi_target_t *t) { active().target_clear_depth(t); }
void rhi_viewport(int x, int y, int w, int h) { active().viewport(x, y, w, h); }
void rhi_target_snapshot(const rhi_target_t *t, rhi_texture_t *dst) { active().target_snapshot(t, dst); }
void rhi_target_free(rhi_target_t *t) { active().target_free(t); }
void rhi_target_begin(const rhi_target_t *t, float r, float g, float b) { active().target_begin(t, r, g, b); }
void rhi_target_resume(const rhi_target_t *t) { active().target_resume(t); }
void rhi_backbuffer(int w, int h) { active().backbuffer(w, h); }
void rhi_read_pixels(int x, int y, int w, int h, unsigned char *rgba) { active().read_pixels(x, y, w, h, rgba); }
void rhi_read_depth(const rhi_target_t *t, float *depth) { active().read_depth(t, depth); }
int rhi_tessellation_supported(void) { return active().tessellation_supported(); }
int rhi_geometry_shader_supported(void) { return active().geometry_shader_supported(); }
rhi_pipeline_t rhi_pipeline_make(const rhi_pipeline_desc_t *d) { return active().pipeline_make(d); }
void rhi_frame_stats_reset(void) { active().frame_stats_reset(); }
rhi_stats_t rhi_frame_stats(void) { return active().frame_stats(); }
int rhi_gpu_timers_supported(void) { return active().gpu_timers_supported(); }
int rhi_gpu_scope(const char *name) { return active().gpu_scope(name); }
const char *rhi_gpu_scope_name(int s) { return active().gpu_scope_name(s); }
int rhi_gpu_scope_count(void) { return active().gpu_scope_count(); }
float rhi_gpu_ms(int s) { return active().gpu_ms(s); }
void rhi_gpu_begin(int s) { active().gpu_begin(s); }
void rhi_gpu_end(void) { active().gpu_end(); }
void rhi_gpu_collect(void) { active().gpu_collect(); }
void rhi_pipeline_bind(const rhi_pipeline_t *p) { active().pipeline_bind(p); }
int rhi_uniform(const rhi_pipeline_t *p, const char *name) { return active().uniform(p, name); }
void rhi_set_f1(int loc, float x)                            { active().set_f1(loc, x); }
void rhi_set_f2(int loc, float x, float y)                   { active().set_f2(loc, x, y); }
void rhi_set_f3(int loc, float x, float y, float z)          { active().set_f3(loc, x, y, z); }
void rhi_set_f4(int loc, float x, float y, float z, float w) { active().set_f4(loc, x, y, z, w); }
void rhi_set_i1(int loc, int x)                              { active().set_i1(loc, x); }
void rhi_set_mat4(int loc, const float *m16)                 { active().set_mat4(loc, m16); }
void rhi_set_f3v(int loc, const float *v, int n)             { active().set_f3v(loc, v, n); }
void rhi_set_f4v(int loc, const float *v, int n)             { active().set_f4v(loc, v, n); }
void rhi_set_f1v(int loc, const float *v, int n)             { active().set_f1v(loc, v, n); }
rhi_texture_t rhi_texture_make(int w, int h, rhi_format_t fmt, rhi_filter_t filter, rhi_wrap_t wrap, const void *data) { return active().texture_make(w, h, fmt, filter, wrap, data); }
void rhi_texture_update(rhi_texture_t *t, const void *data) { active().texture_update(t, data); }
void rhi_texture_update_rect(rhi_texture_t *t, int x, int y, int w, int h, const void *data) { active().texture_update_rect(t, x, y, w, h, data); }
void rhi_texture_bind(int slot, const rhi_texture_t *t) { active().texture_bind(slot, t); }
rhi_geometry_t rhi_geometry_make(const rhi_layout_t *layout, const void *verts, int nverts, const unsigned *indices, int nindices) { return active().geometry_make(layout, verts, nverts, indices, nindices); }
void rhi_geometry_free(rhi_geometry_t *g) { active().geometry_free(g); }
void rhi_geometry_update(rhi_geometry_t *g, const void *verts, int nverts) { active().geometry_update(g, verts, nverts); }
void rhi_geometry_instances(rhi_geometry_t *g, const void *data, int n) { active().geometry_instances(g, data, n); }
int rhi_occlusion_supported(void) { return active().occlusion_supported(); }
unsigned rhi_query_make(void) { return active().query_make(); }
void rhi_query_begin(unsigned q) { active().query_begin(q); }
void rhi_query_end(void) { active().query_end(); }
int rhi_query_result(unsigned q, int *passed) { return active().query_result(q, passed); }
void rhi_color_write(int on) { active().color_write(on); }
void rhi_draw(rhi_geometry_t *g, int ninstances) { active().draw(g, ninstances); }
}
