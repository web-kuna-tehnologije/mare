// mare::engine::render::Wake - see wake.h. One RGBA8 target, two passes a frame: an alpha
// blend of transparent black over the whole map (which multiplies it down - the fade), then
// one small quad per ship painting her churn at the place she is now. The history is the
// texture itself; nothing is stored per ship.
#include "../platform/v3cpp.hh"
#include <cmath>
#include <cstdlib>

extern "C" {
#include "wake.h"
#include "render.h"
#include "gpu.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../engine/shaders.h"
}

namespace mare::engine::render {

#define WAKE_RES   1024
#define WAKE_TAU   28.f      // seconds to fall to 1/e: a wake stays visible for a minute or so
#define WAKE_PAINT 3.2f      // how fast a ship paints to full white (per second of exposure)
#define WAKE_DIFF  0.55f     // texels of spread per second (0.22.11): the trail widens into a cone as it ages and thins as it spreads

class Wake {
public:
    void set_region(float cx, float cz, float half);
    void update(float dt, const float *xzhs, const float *len, int n);
    int  active() const { return on_ && t_[0].fbo; }
    const rhi_texture_t *texture() { return active() ? &t_[cur_].color : 0; }
    void rect(float out[4]) { out[0] = cx_ - half_; out[1] = cz_ - half_; out[2] = out[3] = 1.f / maxf(2.f * half_, 1e-3f); }

private:
    void init();
    rhi_target_t t_[2]{}; int cur_ = 0;   // ping-pong (0.22.11): the fade reads one and writes the other, so it can blur as it fades
    rhi_pipeline_t decay_prg_{}, stamp_prg_{};
    rhi_geometry_t fs_tri_{}, quad_{};
    int inited_ = 0, on_ = 0, cleared_ = 0;
    float cx_ = 0, cz_ = 0, half_ = 0;
};

Wake &wake() { static Wake w; return w; }

void Wake::init() {
    if (inited_) return;
    static const rhi_attr_t attrs[] = { { "a_pos", 0, 2, 0, 0 } };
    static const rhi_layout_t layout = { attrs, 1, { 2 * sizeof(float), 0 } };
    static const char *decay_uni[] = { "u_fade", "u_src", "u_texel", 0 };
    const rhi_pipeline_desc_t dd{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("fx/wake_decay.frag"),
                                  .layout = &layout, .uniforms = decay_uni, .blend = RHI_BLEND_NONE };
    decay_prg_ = rhi_pipeline_make(&dd);
    static const char *stamp_uni[] = { "u_rect", "u_quad", "u_ship", 0 };
    const rhi_pipeline_desc_t sd{ .vs = shader_source("fx/wake_stamp.vert"), .fs = shader_source("fx/wake_stamp.frag"),
                                  .layout = &layout, .uniforms = stamp_uni, .blend = RHI_BLEND_ALPHA };
    stamp_prg_ = rhi_pipeline_make(&sd);
    float tri[6] = { -1, -1, 3, -1, -1, 3 };
    fs_tri_ = rhi_geometry_make(&layout, tri, 3, 0, 0);
    float q[12] = { -1, -1,  1, -1,  1, 1,  -1, -1,  1, 1,  -1, 1 };
    quad_ = rhi_geometry_make(&layout, q, 6, 0, 0);
    t_[0] = rhi_target_make(WAKE_RES, WAKE_RES, RHI_FMT_RGBA8, 0);
    t_[1] = rhi_target_make(WAKE_RES, WAKE_RES, RHI_FMT_RGBA8, 0);
    inited_ = 1;
    LOG_INFO("gfx", "wake map: %dx%d, fade tau %.0f s", WAKE_RES, WAKE_RES, WAKE_TAU);
}

void Wake::set_region(float cx, float cz, float half) {
    if (half <= 0.f) { on_ = 0; return; }
    init();
    if (!on_ || fabsf(cx - cx_) > 0.01f || fabsf(cz - cz_) > 0.01f || fabsf(half - half_) > 0.01f) {
        cx_ = cx; cz_ = cz; half_ = half; cleared_ = 0;   // a new scene: wipe whatever the last one left
    }
    on_ = 1;
}

void Wake::update(float dt, const float *xzhs, const float *len, int n) {
    if (!active()) return;
    if (!cleared_) { rhi_target_begin(&t_[0], 0.f, 0.f, 0.f); rhi_target_begin(&t_[1], 0.f, 0.f, 0.f); cleared_ = 1; }
    // fade + spread (0.22.11, issue8 "fading out and cone spreading and disappearing"): the
    // new map is the old one blurred by a little and multiplied by exp(-dt/tau). The blur is
    // what turns a line astern into a cone - every frame the churn leaks a fraction of a texel
    // outward, so what is old is wide and faint and what is fresh is narrow and bright.
    if (dt > 0.f) {
        int nxt = cur_ ^ 1;
        rhi_target_resume(&t_[nxt]);
        rhi_pipeline_bind(&decay_prg_);
        rhi_set_f1(UNI(decay_prg_, "u_fade"), expf(-dt / WAKE_TAU));
        rhi_set_i1(UNI(decay_prg_, "u_src"), 0); rhi_texture_bind(0, &t_[cur_].color);
        float spread = clampf(dt * WAKE_DIFF, 0.f, 0.5f);
        rhi_set_f2(UNI(decay_prg_, "u_texel"), spread / WAKE_RES, spread / WAKE_RES);
        rhi_draw(&fs_tri_, 0);
        cur_ = nxt;
    } else rhi_target_resume(&t_[cur_]);
    // paint: one quad per ship, two hull lengths square, centred a little abaft her
    if (dt > 0.f && n > 0) {
        rhi_pipeline_bind(&stamp_prg_);
        float r[4]; rect(r);
        rhi_set_f4(UNI(stamp_prg_, "u_rect"), r[0], r[1], r[2], r[3]);
        for (int i = 0; i < n; ++i) {
            float L = maxf(len[i], 0.2f), spd = clampf(xzhs[i * 4 + 3] * 10.f, 0.f, 1.f);
            if (spd < 0.05f) continue;
            float h = xzhs[i * 4 + 2], fx = cosf(h), fz = -sinf(h);
            float cx = xzhs[i * 4] - fx * 0.45f * L, cz = xzhs[i * 4 + 1] - fz * 0.45f * L;   // the box covers stem to a hull astern
            rhi_set_f4(UNI(stamp_prg_, "u_quad"), cx, cz, h, L);
            rhi_set_f4(UNI(stamp_prg_, "u_ship"), L, spd, clampf(dt * WAKE_PAINT, 0.f, 1.f), 0.f);
            rhi_draw(&quad_, 0);
        }
    }
    rhi_backbuffer(screen_w, screen_h);
}

} // namespace mare::engine::render

extern "C" {
void wake_set_region(float cx, float cz, float half) { mare::engine::render::wake().set_region(cx, cz, half); }
void wake_update(float dt, const float *xzhs, const float *len, int n) { mare::engine::render::wake().update(dt, xzhs, len, n); }
int  wake_active(void) { return mare::engine::render::wake().active(); }
const rhi_texture_t *wake_texture(void) { return mare::engine::render::wake().texture(); }
void wake_rect(float out[4]) { mare::engine::render::wake().rect(out); }
}
