// mare::engine::render::Particles - see particles.h. One instanced quad, one pipeline
// (fx/puff.vert + fx/puff.frag), a fixed pool sorted back to front each frame, and the list
// of shockwaves the tonemap distorts the frame with (hdr_set_shocks).
//
// 0.22.8 (issue5, the Meshy reference frame): a broadside is *layers* of smoke, not one puff -
// a dense white cumulus core that rolls out with the shot, a grey plume behind it, a thin haze
// that lingers and drifts off downwind long after the rest has gone - plus the water's answer
// (a ring of flattened sea and a sheet of spray under a low gun) and the air's (a shockwave
// the tonemap bends the picture with for a quarter of a second).
#include "../platform/v3cpp.hh"
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "particles.h"
#include "render.h"
#include "gpu.h"
#include "glsl.h"
#include "camera.h"
#include "lod.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../engine/shaders.h"
}

namespace mare::engine::render {

// kinds, as fx/puff.frag reads them in i_misc.z
enum { PK_SMOKE = 0, PK_FLASH = 1, PK_EMBER = 2, PK_CORE = 3, PK_HAZE = 4, PK_SPRAY = 5, PK_RING = 6 };
#define PTC_MAX   4096
#define SHOCK_MAX 16

typedef struct ptc_t {
    float x, y, z, vx, vy, vz;
    float age, life;
    float size0, size1;      // at birth / at death (the growth curve is in size_at)
    float rot, rotv;         // billboard spin
    float seed, alpha;
    float shade;             // smoke albedo at birth (a gun's smoke is pale, sooty at the core)
    unsigned char kind;
} ptc_t;

typedef struct shock_t { float x, y, z, age, life, r0, r1, strength; } shock_t;

class Particles {
public:
    void init();
    void reset() { n_ = 0; nshock_ = 0; }
    void set_wind(float vx, float vz) { wind_x_ = vx; wind_z_ = vz; }
    void broadside(float x, float y, float z, float heading, float side, float hull_len, int guns);
    void update(float dt);
    int  draw();
    int  count() const { return n_; }

private:
    ptc_t *spawn();
    void   shock(float x, float y, float z, float r1, float strength);
    static float size_at(const ptc_t *p, float t) { float g = 1.f - (1.f - t) * (1.f - t); return lerpf(p->size0, p->size1, g); }

    ptc_t p_[PTC_MAX]; int n_ = 0;
    shock_t sh_[SHOCK_MAX]; int nshock_ = 0;
    float wind_x_ = 0, wind_z_ = 0;
    unsigned rng_ = 0x9e3779b9u;
    rhi_pipeline_t prg_{}; mesh_t quad_{}; int inited_ = 0;
    instlist_t il_{};
    int order_[PTC_MAX]; float dist_[PTC_MAX];
};

Particles &particles() { static Particles p; return p; }

void Particles::init() {
    if (inited_) return;
    static const char *uniforms[] = { "u_vp", "u_cam_right", "u_cam_up", "u_campos", "u_sky", "u_mapness", "u_time",
                                      "u_scene_depth", "u_zrange", "u_scene_size", "u_soft_on", GLSL_LIGHT_UNIFORMS, 0 };
    const rhi_pipeline_desc_t desc{ .vs = shader_source("fx/puff.vert"), .fs = shader_source("fx/puff.frag"), .layout = &mesh_layout,
                                    .uniforms = uniforms, .depth_test = 1, .depth_write = 0, .blend = RHI_BLEND_ALPHA };
    prg_ = rhi_pipeline_make(&desc);
    // the billboard: a unit quad in the xy plane; the vertex stage reads a_pos.xy as the offset
    mb_t m = {0};
    mb_material(&m, 1.f, 0.f);
    const float a[3] = { -1, -1, 0 }, b[3] = { 1, -1, 0 }, c[3] = { 1, 1, 0 }, d[3] = { -1, 1, 0 };
    mb_quad2(&m, a, b, c, d, 1, 1, 1, 1);
    quad_ = mesh_upload(&m);
    inited_ = 1;
    LOG_INFO("gfx", "particles: pool %d, %d shockwaves, billboard pipeline ready", PTC_MAX, SHOCK_MAX);
}

ptc_t *Particles::spawn() {
    if (n_ >= PTC_MAX) {   // recycle the oldest smoke rather than drop the new flash
        int worst = 0; float wa = -1.f;
        for (int i = 0; i < n_; ++i) { float a = p_[i].age / maxf(p_[i].life, 0.01f); if (a > wa) { wa = a; worst = i; } }
        return &p_[worst];
    }
    return &p_[n_++];
}

void Particles::shock(float x, float y, float z, float r1, float strength) {
    shock_t *s;
    if (nshock_ < SHOCK_MAX) s = &sh_[nshock_++];
    else { s = &sh_[0]; for (int i = 1; i < SHOCK_MAX; ++i) if (sh_[i].age / sh_[i].life > s->age / s->life) s = &sh_[i]; }
    *s = shock_t{ x, y, z, 0.f, 0.32f, 0.3f, r1, strength };
}

// A broadside along one side. The guns stand along the middle two thirds of the hull on the
// gun deck (about a tenth of the length above the waterline on these hulls). Every gun gives
// a muzzle flash; a dense white core that races out with the shot and keeps rolling; a grey
// plume behind it; a thin haze layer that outlives both; a handful of embers. Under a low gun
// the blast flattens the sea - a ring - and throws spray. One shockwave per few guns. Bigger
// hulls carry bigger guns: the puff size grows with the length, the count with the guns.
void Particles::broadside(float x, float y, float z, float heading, float side, float hull_len, int guns) {
    int nside = (int)clampf((float)guns * 0.5f, 1.f, 18.f);
    float L = maxf(hull_len, 3.f);
    float gs = clampf(0.55f + L / 38.f, 0.7f, 1.9f);                 // gun size factor: sloop 0.8, ship of the line ~1.05, great carrack 1.3
    float fx = cosf(heading), fz = -sinf(heading);                    // the heading frame the staged ships use (0 = +x)
    float sx = -fz * side, sz = fx * side;                            // outward, this side
    float half = 0.34f * L, beam = 0.13f * L, deck = 0.10f * L + 0.4f;
    for (int g = 0; g < nside; ++g) {
        float along = nside > 1 ? -half + 2.f * half * (float)g / (float)(nside - 1) : 0.f;
        float gx = x + fx * along + sx * beam, gz = z + fz * along + sz * beam, gy = y + deck + (rnd_f(&rng_) - 0.5f) * 0.1f;
        // flash
        { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_FLASH; p->x = gx + sx * 0.8f; p->y = gy; p->z = gz + sz * 0.8f;
          p->vx = sx * 6.f; p->vz = sz * 6.f; p->life = 0.11f + rnd_f(&rng_) * 0.05f;
          p->size0 = 1.0f * gs; p->size1 = 2.6f * gs; p->alpha = 1.f; p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; }
        // layer 1 - the core: three dense white cumulus balls, fast, slow to thin, big enough
        // to swallow the gun deck the way the reference frame's do
        for (int k = 0; k < 3; ++k) {
            ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_CORE;
            float spd = (k == 0 ? 14.f : 10.f - 2.f * k) * (0.85f + 0.3f * rnd_f(&rng_));
            float jx = (rnd_f(&rng_) - 0.5f), jz = (rnd_f(&rng_) - 0.5f);
            p->x = gx + sx * (0.7f + k * 0.6f); p->y = gy + (rnd_f(&rng_) - 0.3f) * 0.25f + 0.3f * k; p->z = gz + sz * (0.7f + k * 0.6f);
            p->vx = sx * spd + fx * jx * 1.8f; p->vz = sz * spd + fz * jz * 1.8f; p->vy = 0.6f + rnd_f(&rng_) * 0.8f;
            p->life = 3.6f + rnd_f(&rng_) * 1.6f + 0.4f * k;
            p->size0 = (0.5f + 0.25f * k) * gs; p->size1 = (4.6f + 0.9f * k + rnd_f(&rng_) * 0.8f) * gs;
            p->alpha = 0.95f; p->shade = 0.96f - 0.04f * k;
            p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f) * 0.9f;
        }
        // layer 2 - the plume: three grey puffs trailing the core
        for (int k = 0; k < 3; ++k) {
            ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SMOKE;
            float spd = (8.f - 2.f * k) * (0.85f + 0.3f * rnd_f(&rng_));
            float jx = (rnd_f(&rng_) - 0.5f), jz = (rnd_f(&rng_) - 0.5f);
            p->x = gx + sx * (0.5f + k * 0.4f); p->y = gy + (rnd_f(&rng_) - 0.3f) * 0.3f; p->z = gz + sz * (0.5f + k * 0.4f);
            p->vx = sx * spd + jx * 1.2f + fx * jx * 1.5f; p->vz = sz * spd + jz * 1.2f + fz * jz * 1.5f; p->vy = 0.4f + rnd_f(&rng_) * 0.8f;
            p->life = 5.f + rnd_f(&rng_) * 3.f + k * 0.6f;
            p->size0 = (0.35f + 0.15f * k) * gs; p->size1 = (4.2f + 1.4f * k + rnd_f(&rng_)) * gs;
            p->alpha = 0.72f; p->shade = 0.80f - 0.07f * k;
            p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f) * 0.6f;
        }
        // layer 3 - the haze: one big thin veil that hangs about and drifts off on the wind
        { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_HAZE;
          p->x = gx + sx * 1.5f; p->y = gy + 0.6f; p->z = gz + sz * 1.5f;
          p->vx = sx * 2.5f; p->vz = sz * 2.5f; p->vy = 0.25f;
          p->life = 10.f + rnd_f(&rng_) * 5.f;
          p->size0 = 1.5f * gs; p->size1 = (9.f + rnd_f(&rng_) * 4.f) * gs;
          p->alpha = 0.38f; p->shade = 0.74f; p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f) * 0.2f; }
        // embers: burning wad thrown with the shot, out in a third of a second
        int ne = 2 + (int)(gs * 2.f);
        for (int k = 0; k < ne; ++k) {
            ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_EMBER;
            float spd = 10.f + rnd_f(&rng_) * 9.f, up = (rnd_f(&rng_) - 0.3f) * 3.f, sw = (rnd_f(&rng_) - 0.5f) * 3.f;
            p->x = gx + sx * 0.8f; p->y = gy; p->z = gz + sz * 0.8f;
            p->vx = sx * spd + fx * sw; p->vz = sz * spd + fz * sw; p->vy = up;
            p->life = 0.25f + rnd_f(&rng_) * 0.3f; p->size0 = 0.09f * gs; p->size1 = 0.04f * gs; p->alpha = 1.f; p->seed = rnd_f(&rng_);
        }
        // the water's answer: the blast of a gun a few metres over the sea flattens a ring of it
        // and throws a sheet of spray outboard (the reference frame's splashes along the side)
        if (deck < 3.2f * gs && g % 2 == 0) {   // every other gun: the rings would otherwise chain into a white road along the side
            float wx = gx + sx * (2.0f + 0.8f * gs), wz = gz + sz * (2.0f + 0.8f * gs);
            { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_RING; p->x = wx; p->y = 0.06f; p->z = wz;
              p->life = 0.8f + 0.25f * rnd_f(&rng_); p->size0 = 0.4f * gs; p->size1 = 2.4f * gs; p->alpha = 0.38f; p->seed = rnd_f(&rng_); }
            int ns = 3 + (int)(gs * 3.f);   // the spout: a column of spray thrown up by the blast
            for (int k = 0; k < ns; ++k) {
                ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SPRAY;
                float a = rnd_f(&rng_) * 6.2832f, rr = rnd_f(&rng_) * 0.7f * gs;
                p->x = wx + cosf(a) * rr; p->y = 0.1f; p->z = wz + sinf(a) * rr;
                p->vx = sx * (1.f + rnd_f(&rng_) * 2.f) + cosf(a) * 0.8f; p->vz = sz * (1.f + rnd_f(&rng_) * 2.f) + sinf(a) * 0.8f;
                p->vy = 4.5f + rnd_f(&rng_) * 5.f * gs;
                p->life = 1.0f + rnd_f(&rng_) * 0.5f; p->size0 = 0.3f * gs; p->size1 = 1.3f * gs; p->alpha = 0.8f; p->seed = rnd_f(&rng_);
            }
        }
        // the air's answer: one shockwave per three guns, from the muzzle
        if (g % 3 == 0) shock(gx + sx * 1.2f, gy, gz + sz * 1.2f, (4.f + 2.5f * gs) * 1.0f, clampf(0.5f + 0.35f * gs, 0.5f, 1.2f));
    }
}

void Particles::update(float dt) {
    if (dt <= 0.f) return;
    const float G = 9.81f / 2.47f;   // gravity in local units (1 unit ~ 2.47 m)
    for (int i = 0; i < n_; ) {
        ptc_t *p = &p_[i];
        p->age += dt;
        if (p->age >= p->life) { *p = p_[--n_]; continue; }
        float t = p->age / p->life;
        switch (p->kind) {
            case PK_SMOKE: case PK_CORE: case PK_HAZE: {
                // drag towards the air it sits in (the wind), buoyancy that fades as the hot
                // gas cools, a little spin; the plume slows from a jet to a drift in ~1.5 s.
                // The core is heavier and slower to give up its speed, the haze is all wind.
                float kd = p->kind == PK_CORE ? 1.2f : p->kind == PK_HAZE ? 2.5f : 1.6f;
                float k = 1.f - expf(-dt * kd);
                p->vx += (wind_x_ - p->vx) * k; p->vz += (wind_z_ - p->vz) * k;
                float buoy = (p->kind == PK_CORE ? 1.4f : 1.1f) * (1.f - t) * (1.f - t) + 0.12f;
                p->vy += (buoy - p->vy) * (1.f - expf(-dt * 1.2f));
                p->rot += p->rotv * dt;
            } break;
            case PK_FLASH: p->vx *= expf(-dt * 8.f); p->vz *= expf(-dt * 8.f); break;
            case PK_EMBER: p->vy -= G * dt; { float d = expf(-dt * 2.5f); p->vx = wind_x_ + (p->vx - wind_x_) * d; p->vz = wind_z_ + (p->vz - wind_z_) * d; } break;
            case PK_SPRAY: p->vy -= G * 0.8f * dt; { float d = expf(-dt * 1.5f); p->vx *= d; p->vz *= d; } break;
            case PK_RING: break;
        }
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        if ((p->kind == PK_EMBER || p->kind == PK_SPRAY) && p->y < 0.02f) { *p = p_[--n_]; continue; }   // quenched / back in the sea
        ++i;
    }
    for (int i = 0; i < nshock_; ) { sh_[i].age += dt; if (sh_[i].age >= sh_[i].life) sh_[i] = sh_[--nshock_]; else ++i; }
}

int Particles::draw() {
    if (!inited_) return 0;
    // the shockwaves, to the tonemap: screen position, ring radius in pixels, strength
    { float v[SHOCK_MAX * 4]; int n = 0;
      for (int i = 0; i < nshock_; ++i) { const shock_t *s = &sh_[i];
          float t = s->age / s->life, sx, sy;
          if (!cam_project(s->x, s->y, s->z, &sx, &sy)) continue;
          float dx = s->x - RF.campos.x, dy = s->y - RF.campos.y, dz = s->z - RF.campos.z, dist = sqrtf(dx*dx + dy*dy + dz*dz);
          float rw = lerpf(s->r0, s->r1, 1.f - (1.f - t) * (1.f - t));          // the front slows as it spreads
          float rpx = lod_px(rw, dist) * 0.5f;
          float str = s->strength * (1.f - t) * (1.f - t) * clampf(1.f - dist / 400.f, 0.f, 1.f);
          if (rpx < 2.f || str < 0.01f) continue;
          v[n*4] = sx / maxf((float)screen_w, 1.f); v[n*4+1] = 1.f - sy / maxf((float)screen_h, 1.f); v[n*4+2] = rpx; v[n*4+3] = str; n++; }
      hdr_set_shocks(v, n); }
    if (n_ == 0) return 0;
    // back to front: alpha-blended discs without depth writes
    for (int i = 0; i < n_; ++i) { order_[i] = i; float dx = p_[i].x - RF.campos.x, dy = p_[i].y - RF.campos.y, dz = p_[i].z - RF.campos.z; dist_[i] = dx*dx + dy*dy + dz*dz; }
    // insertion sort on nearly-sorted data (the order barely changes between frames); the pool is small
    for (int i = 1; i < n_; ++i) { int v = order_[i]; float dv = dist_[v]; int j = i - 1; while (j >= 0 && dist_[order_[j]] < dv) { order_[j+1] = order_[j]; --j; } order_[j+1] = v; }
    instlist_clear(&il_);
    for (int k = 0; k < n_; ++k) {
        const ptc_t *p = &p_[order_[k]];
        float t = p->age / p->life, size = size_at(p, t), alpha, r, g, b;
        switch (p->kind) {
            default:
            case PK_SMOKE: { float a = p->alpha * powf(1.f - t, 1.25f) * clampf(p->age / 0.08f, 0.f, 1.f);
                             float sh = lerpf(p->shade, 0.62f, t);   // greys as it thins
                             alpha = a; r = sh; g = sh * 0.99f; b = sh * 0.98f; } break;
            case PK_CORE:  { float a = p->alpha * powf(1.f - t, 0.9f) * clampf(p->age / 0.05f, 0.f, 1.f);
                             float sh = lerpf(p->shade, 0.78f, t);
                             alpha = a; r = sh; g = sh; b = sh * 0.99f; } break;
            case PK_HAZE:  { float a = p->alpha * sinf(3.14159f * minf(t, 1.f)) * clampf(p->age / 0.4f, 0.f, 1.f);
                             alpha = a; r = p->shade; g = p->shade; b = p->shade * 1.01f; } break;
            case PK_FLASH: alpha = 1.f - t * t; r = 1.0f; g = 0.78f; b = 0.42f; break;
            case PK_EMBER: alpha = 1.f - t; r = 1.0f; g = 0.55f + 0.3f * (1.f - t); b = 0.18f; break;
            case PK_SPRAY: alpha = p->alpha * (1.f - t * t); r = 0.92f; g = 0.95f; b = 0.97f; break;
            case PK_RING:  alpha = p->alpha * (1.f - t) * (1.f - t) * clampf(p->age / 0.05f, 0.f, 1.f); r = 0.90f; g = 0.93f; b = 0.95f; break;
        }
        // inst_t: pos+rotation | size+rgb | alpha, seed, kind, age01 | soft radius
        *instlist_push(&il_) = inst_t{ p->x, p->y, p->z, p->rot, size, r, g, b, alpha, p->seed, (float)p->kind, t, size, 0, 0, 0, 0, 0 };
    }
    rhi_pipeline_bind(&prg_);
    rhi_set_mat4(UNI(prg_, "u_vp"), &RF.vp.x.x);
    rhi_set_f3(UNI(prg_, "u_cam_right"), cam.right.x, cam.right.y, cam.right.z);
    rhi_set_f3(UNI(prg_, "u_cam_up"), cam.up.x, cam.up.y, cam.up.z);
    rhi_set_f3(UNI(prg_, "u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(prg_, "u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(UNI(prg_, "u_mapness"), RF.mapness);
    rhi_set_f1(UNI(prg_, "u_time"), RF.anim_time);
    rhi_set_f2(UNI(prg_, "u_zrange"), cam.znear, cam.zfar);
    rhi_set_f2(UNI(prg_, "u_scene_size"), (float)screen_w, (float)screen_h);
    { const rhi_texture_t *dp = hdr_depth_captured();   // the opaque scene's depth (taken before the water): soft edges against the hulls
      if (dp) { rhi_set_i1(UNI(prg_, "u_scene_depth"), 7); rhi_texture_bind(7, dp); rhi_set_f1(UNI(prg_, "u_soft_on"), 1.f); }
      else rhi_set_f1(UNI(prg_, "u_soft_on"), 0.f); }
    light_bind(&prg_);
    mesh_set_instances(&quad_, il_.v, il_.n);
    mesh_draw(&quad_);
    return il_.n;
}

} // namespace mare::engine::render

extern "C" {
void particles_init(void) { mare::engine::render::particles().init(); }
void particles_reset(void) { mare::engine::render::particles().reset(); }
void particles_set_wind(float vx, float vz) { mare::engine::render::particles().set_wind(vx, vz); }
void particles_broadside(float x, float y, float z, float heading, float side, float hull_len, int guns) { mare::engine::render::particles().broadside(x, y, z, heading, side, hull_len, guns); }
void particles_update(float dt) { mare::engine::render::particles().update(dt); }
int  particles_draw(void) { return mare::engine::render::particles().draw(); }
int  particles_count(void) { return mare::engine::render::particles().count(); }
}
