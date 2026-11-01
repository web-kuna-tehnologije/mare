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
#include "../assets/asset.h"   // asset_root: the sprite atlases (0.22.13)
#include "render.h"
#include "gpu.h"
#include "glsl.h"
#include "camera.h"
#include "lod.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../engine/shaders.h"
#include "../sim/naval_physics.h"
}

namespace mare::engine::render {

// kinds, as fx/puff.frag reads them in i_misc.z
enum { PK_SMOKE = 0, PK_FLASH = 1, PK_EMBER = 2, PK_CORE = 3, PK_HAZE = 4, PK_SPRAY = 5, PK_RING = 6, PK_SPLINTER = 7 };
#define PTC_MAX   5632
#define BLAST_MAX 12
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
// One broadside's mark on the water (0.22.9, issue6): a ring running out from the ship on the
// side she fired, drawn by the sea itself (sea_set_blasts) rather than laid over it.
typedef struct blast_t { float x, z, dx, dz, age, life, rmax, inner, strength; } blast_t;
// A gun waiting to fire (0.22.10, issue7 "cannons fire not all at once but with a random
// timeout"): a broadside is laid *rolling* - each gun has its own delay, a few tens of
// milliseconds to a second and a half - so the side goes off as a ripple of flashes rather
// than one wall. The gun's geometry is fixed when it is queued; only the moment is deferred.
typedef struct gun_t { float gx, gy, gz, sx, sz, fx, fz, gs, deck; float t; int ring; int body; float cx, cz, side; } gun_t;
#define GUN_MAX 768
// A blob of the volumetric smoke (0.22.15, issue12 "3D volumetric smoke with shaders"): a sphere
// of density that grows from r0 to r1 (fast at first), drifts on the wind and thins with age; the
// fullscreen ray-march in fx/volsmoke.frag draws the set. Replaces the smoke billboards
// (MARE_SMOKE_BILLBOARDS=1 brings those back).
typedef struct blob_t { float x, y, z, vx, vy, vz, age, life, r0, r1, dens, seed, shade; int kind; } blob_t;
#define BLOB_MAX 128

class Particles {
public:
    void init();
    void reset() { n_ = 0; nshock_ = 0; nblast_ = 0; ngun_ = 0; nblob_ = 0; sea_set_blasts(0, 0, 0); }
    void set_wind(float vx, float vz) { wind_x_ = vx; wind_z_ = vz; }
    void broadside(float x, float y, float z, float heading, float side, float hull_len, int guns, int body);
    void splash(float x, float z, float size);
    void hit(float x, float y, float z, float dx, float dz, float size);
    void spray(float x, float y, float z, float dx, float dz, float strength);
    void update(float dt);
    int  draw();
    int  count() const { return n_; }

private:
    ptc_t *spawn();
    void   shock(float x, float y, float z, float r1, float strength);
    void   blast(float x, float z, float dx, float dz, float L);
    void   push_blasts();
    void   fire_gun(const gun_t &g);
    static float size_at(const ptc_t *p, float t) { float g = 1.f - (1.f - t) * (1.f - t); return lerpf(p->size0, p->size1, g); }

    ptc_t p_[PTC_MAX]; int n_ = 0;
    shock_t sh_[SHOCK_MAX]; int nshock_ = 0;
    blast_t bl_[BLAST_MAX]; int nblast_ = 0;
    gun_t  gq_[GUN_MAX]; int ngun_ = 0;
    blob_t vb_[BLOB_MAX]; int nblob_ = 0; int vol_on_ = 1;
    rhi_pipeline_t vol_prg_{}; rhi_geometry_t vol_tri_{}; int vol_inited_ = 0;
    blob_t *blob_spawn();
    void   vol_draw();
    float wind_x_ = 0, wind_z_ = 0;
    unsigned rng_ = 0x9e3779b9u;
    rhi_pipeline_t prg_{}; mesh_t quad_{}; int inited_ = 0;
    int tex_smoke_ = -1, tex_fire_ = -1, tex_foam_ = -1;   // 0.22.13 (issue10): the Meshy sheets as RGBA sprite atlases (tools/fx_sprites.py)
    instlist_t il_{};
    int order_[PTC_MAX]; float dist_[PTC_MAX];
};

Particles &particles() { static Particles p; return p; }

void Particles::init() {
    if (inited_) return;
    static const char *uniforms[] = { "u_vp", "u_cam_right", "u_cam_up", "u_campos", "u_sky", "u_mapness", "u_time",
                                      "u_scene_depth", "u_zrange", "u_scene_size", "u_soft_on",
                                      "u_fx_smoke", "u_fx_fire", "u_fx_foam", "u_has_fx", GLSL_LIGHT_UNIFORMS, 0 };
    const rhi_pipeline_desc_t desc{ .vs = shader_source("fx/puff.vert"), .fs = shader_source("fx/puff.frag"), .layout = &mesh_layout,
                                    .uniforms = uniforms, .depth_test = 1, .depth_write = 0, .blend = RHI_BLEND_ALPHA };
    prg_ = rhi_pipeline_make(&desc);
    // 0.22.13 (issue10 "there are smoke and fire and foam images, use them"): the sheets under
    // assets/ships/{smoke,fire,foam/masks} cut into RGBA atlases (tools/fx_sprites.py -> <assets>/fx),
    // 2x2 smoke, 4x2 fire, 2x2 foam; the fragment stage samples them by the particle's seed
    { char path[600];
      snprintf(path, sizeof path, "%s/fx/smoke_atlas.png", asset_root()); tex_smoke_ = texture_request(path, RHI_FMT_SRGB8_A8, RHI_FILTER_MIPMAP, RHI_WRAP_CLAMP);
      snprintf(path, sizeof path, "%s/fx/fire_atlas.png",  asset_root()); tex_fire_  = texture_request(path, RHI_FMT_SRGB8_A8, RHI_FILTER_MIPMAP, RHI_WRAP_CLAMP);
      snprintf(path, sizeof path, "%s/fx/foam_atlas.png",  asset_root()); tex_foam_  = texture_request(path, RHI_FMT_SRGB8_A8, RHI_FILTER_MIPMAP, RHI_WRAP_CLAMP); }
    {   // the volumetric pass (0.22.15): a fullscreen triangle, the blobs as uniforms
        { const char *e = getenv("MARE_SMOKE_BILLBOARDS"); if (e && atoi(e)) vol_on_ = 0; }
        static const rhi_attr_t vattrs[] = { { "a_pos", 0, 2, 0, 0 } };
        static const rhi_layout_t vlayout = { vattrs, 1, { 2 * sizeof(float), 0 } };
        static const char *vuniforms[] = { "u_inv_vp", "u_campos", "u_scene_depth", "u_soft_on", "u_blob", "u_blobp", "u_nblob", "u_time", "u_sky", "u_mapness", "u_wind", "u_debug", GLSL_LIGHT_UNIFORMS, 0 };
        const rhi_pipeline_desc_t vdesc{ .vs = shader_source("common/fullscreen.vert"), .fs = shader_source("fx/volsmoke.frag"), .layout = &vlayout,
                                         .uniforms = vuniforms, .depth_test = 0, .depth_write = 0, .blend = RHI_BLEND_ALPHA };
        vol_prg_ = rhi_pipeline_make(&vdesc);
        float tri[] = { -1,-1, 3,-1, -1,3 };
        vol_tri_ = rhi_geometry_make(&vlayout, tri, 3, 0, 0);
        vol_inited_ = 1;
        LOG_INFO("gfx", "volumetric gunsmoke: %s (fx/volsmoke.frag, up to %d blobs)", vol_on_ ? "on" : "off - billboards (MARE_SMOKE_BILLBOARDS)", BLOB_MAX);
    }
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

blob_t *Particles::blob_spawn() {
    if (nblob_ >= BLOB_MAX) {   // recycle the most spent
        int worst = 0; float wa = -1.f;
        for (int i = 0; i < nblob_; ++i) { float a = vb_[i].age / maxf(vb_[i].life, 0.01f); if (a > wa) { wa = a; worst = i; } }
        return &vb_[worst];
    }
    return &vb_[nblob_++];
}

void Particles::shock(float x, float y, float z, float r1, float strength) {
    shock_t *s;
    if (nshock_ < SHOCK_MAX) s = &sh_[nshock_++];
    else { s = &sh_[0]; for (int i = 1; i < SHOCK_MAX; ++i) if (sh_[i].age / sh_[i].life > s->age / s->life) s = &sh_[i]; }
    *s = shock_t{ x, y, z, 0.f, 0.32f, 0.3f, r1, strength };
}

void Particles::blast(float x, float z, float dx, float dz, float L) {
    blast_t *b;
    if (nblast_ < BLAST_MAX) b = &bl_[nblast_++];
    else { b = &bl_[0]; for (int i = 1; i < BLAST_MAX; ++i) if (bl_[i].age / bl_[i].life > b->age / b->life) b = &bl_[i]; }
    *b = blast_t{ x, z, dx, dz, 0.f, 2.0f, 1.6f * L, 0.42f * L, 0.85f };   // 0.22.13 (issue10): runs out 1.6 hull lengths, not 2.3
}

// A broadside along one side. The guns stand along the middle two thirds of the hull on the
// gun deck (about a tenth of the length above the waterline on these hulls). Every gun gives
// a muzzle flash; a dense white core that races out with the shot and keeps rolling; a grey
// plume behind it; a thin haze layer that outlives both; a handful of embers. Under a low gun
// the blast flattens the sea - a ring - and throws spray. One shockwave per few guns. Bigger
// hulls carry bigger guns: the puff size grows with the length, the count with the guns.
// A broadside along one side. The guns stand along the middle two thirds of the hull on the
// gun deck (about a tenth of the length above the waterline on these hulls). Every gun gives
// a muzzle flash; a dense core that races out with the shot and keeps rolling; a grey plume
// behind it; a thin haze layer that outlives both; a handful of embers. Under a low gun the
// blast throws spray. Bigger hulls carry bigger guns: the puff size grows with the length,
// the count with the guns. 0.22.10: the guns are *queued* with their own delays and go off as
// a ripple (fire_gun); the ring on the water is laid when the first of them fires.
void Particles::broadside(float x, float y, float z, float heading, float side, float hull_len, int guns, int body) {
    int nside = (int)clampf((float)guns * 0.5f, 1.f, 18.f);
    float L = maxf(hull_len, 3.f);
    float gs = clampf(0.55f + L / 38.f, 0.7f, 1.9f);                 // gun size factor: sloop 0.8, ship of the line ~1.05, great carrack 1.3
    float fx = cosf(heading), fz = -sinf(heading);                    // the heading frame the staged ships use (0 = +x)
    float sx = -fz * side, sz = fx * side;                            // outward, this side
    float half = 0.34f * L, beam = 0.13f * L, deck = 0.10f * L + 0.4f;
    // the order the guns go off in is not bow to stern: each gets a delay of its own, with a
    // few landing together so the ripple has stutters in it
    float spread = 0.6f + 0.06f * nside;                              // a 36-gun side rolls for ~1.7 s
    int first = -1; float tfirst = 1e9f;
    for (int g = 0; g < nside; ++g) {
        if (ngun_ >= GUN_MAX) break;
        float along = nside > 1 ? -half + 2.f * half * (float)g / (float)(nside - 1) : 0.f;
        gun_t q; q.gx = x + fx * along + sx * beam; q.gz = z + fz * along + sz * beam; q.gy = y + deck + (rnd_f(&rng_) - 0.5f) * 0.1f;
        q.sx = sx; q.sz = sz; q.fx = fx; q.fz = fz; q.gs = gs; q.deck = deck; q.ring = (g % 2 == 0); q.body = body; q.cx = x; q.cz = z; q.side = side;
        float r = rnd_f(&rng_);
        q.t = r < 0.18f ? 0.f : r * r * spread;                       // some at once, the rest strung out
        if (q.t < tfirst) { tfirst = q.t; first = ngun_; }
        gq_[ngun_++] = q;
    }
    (void)first;
    // one ring on the water for the whole broadside, running out from the ship on the side she
    // fired - the sea draws it (sea_set_blasts), so her own swell still runs through it
    blast(x, z, sx, sz, L);
}

// one gun going off
void Particles::fire_gun(const gun_t &g) {
    // 0.22.11: Newton's third law. The gun throws its shot one way and the ship the other -
    // an impulse of the shot's momentum at the gun's own position, which is off her centre
    // of mass, so a rolling broadside also *turns* her a little as it walks along the side
    // (sim/naval_physics.h nphys_impulse). A 32-pounder's ball is 14.5 kg at ~480 m/s; the
    // number here is that, in tonne-units per second at 2.47 m a unit, scaled up x6 so the
    // sideways set of a 1000-tonne hull is a readable third of a unit a second instead of
    // five centimetres. The shot itself goes into the ballistic model (nphys_fire): a swept
    // sphere under gravity, with a little spread and a low elevation, that hits a hull or
    // splashes (staged_scene.cc turns the events into spray and splinters).
    // 0.22.12 (issue9): "move the ship by about 5 cm, not rotate her five times; the rotation
    // is about the x axis, a fraction of a degree per gun". The sideways set is the ball's real
    // momentum (0.35 tonne-units/s a gun - a 1000-tonne hull moves centimetres), applied at the
    // *centre* so it does not yaw her; and each gun gives her a kick of heel, 0.25 degrees per
    // gun scaled by the gun's size, away from the side she is firing, which the roll spring in
    // the physics brings back over a few seconds.
    if (g.body >= 0) {
        float J = 0.35f * g.gs;
        nphys_impulse(g.body, g.cx, g.cz, -g.sx * J, -g.sz * J);
        nphys_kick_roll(g.body, -g.side * 0.0044f * g.gs);
        float yaw = (rnd_f(&rng_) - 0.5f) * 0.06f, el = 0.004f + rnd_f(&rng_) * 0.03f;
        float dx = g.sx * cosf(yaw) - g.sz * sinf(yaw), dz = g.sx * sinf(yaw) + g.sz * cosf(yaw);
        nphys_fire(g.body, g.gx + g.sx * 0.8f, g.gy, g.gz + g.sz * 0.8f, dx, el, dz, 150.f + 30.f * g.gs);
    }
    const float gx = g.gx, gy = g.gy, gz = g.gz, sx = g.sx, sz = g.sz, fx = g.fx, fz = g.fz, gs = g.gs, deck = g.deck;
        // flash
        { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_FLASH; p->x = gx + sx * 0.8f; p->y = gy; p->z = gz + sz * 0.8f;
          p->vx = sx * 6.f; p->vz = sz * 6.f; p->life = 0.15f + rnd_f(&rng_) * 0.07f;
          p->size0 = 1.2f * gs; p->size1 = 3.2f * gs; p->alpha = 1.f; p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; }
        // 0.22.10 (issue7 "more fire"): a tongue of flame racing out ahead of the flash, and a
        // second, lower one licking along the water
        for (int k = 0; k < 2; ++k) { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_FLASH;
          p->x = gx + sx * (1.6f + 0.9f * k); p->y = gy - 0.25f * k; p->z = gz + sz * (1.6f + 0.9f * k);
          p->vx = sx * (14.f - 4.f * k); p->vz = sz * (14.f - 4.f * k); p->vy = -0.4f * k;
          p->life = 0.12f + rnd_f(&rng_) * 0.08f; p->size0 = 0.55f * gs; p->size1 = (1.6f + 0.4f * k) * gs; p->alpha = 0.9f; p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; }
        // 0.22.15: the volumetric smoke - a white core that races out and swells, a grey plume
        // behind it and a thin haze that outlives both (issue12 "faster smoke expansion from
        // cannon fire and longer stay in the air": the growth is cubic-eased, the lives 12-30 s)
        if (vol_on_) {
            for (int k = 0; k < 3; ++k) { blob_t *b = blob_spawn(); *b = blob_t{};
                float spd = (9.f - 2.5f * k) * (0.85f + 0.3f * rnd_f(&rng_)), off = (rnd_f(&rng_) - 0.5f) * 1.2f * gs;
                b->x = gx + sx * (0.8f + 1.1f * k) + fx * off; b->y = gy + 0.25f + 0.35f * k; b->z = gz + sz * (0.8f + 1.1f * k) + fz * off;
                b->vx = sx * spd + fx * (rnd_f(&rng_) - 0.5f) * 1.5f; b->vz = sz * spd + fz * (rnd_f(&rng_) - 0.5f) * 1.5f; b->vy = 0.7f + 0.6f * rnd_f(&rng_);
                b->life = 12.f + 4.f * rnd_f(&rng_); b->r0 = 0.8f * gs; b->r1 = (6.5f + 1.5f * k + 1.5f * rnd_f(&rng_)) * gs;   // 0.22.15: half as big again - the first cut read as beads beside a 19-unit hull
                b->dens = 2.0f; b->shade = 0.86f - 0.05f * k + 0.06f * rnd_f(&rng_); b->seed = rnd_f(&rng_); b->kind = 0; }
            { blob_t *b = blob_spawn(); *b = blob_t{};
                float spd = 5.f * (0.85f + 0.3f * rnd_f(&rng_));
                b->x = gx + sx * 0.6f; b->y = gy + 0.3f; b->z = gz + sz * 0.6f;
                b->vx = sx * spd; b->vz = sz * spd; b->vy = 0.5f + 0.5f * rnd_f(&rng_);
                b->life = 18.f + 6.f * rnd_f(&rng_); b->r0 = 0.7f * gs; b->r1 = (9.5f + 2.f * rnd_f(&rng_)) * gs;
                b->dens = 1.3f; b->shade = 0.58f + 0.08f * rnd_f(&rng_); b->seed = rnd_f(&rng_); b->kind = 1; }
            if (rnd_f(&rng_) < 0.5f) { blob_t *b = blob_spawn(); *b = blob_t{};
                b->x = gx + sx * 1.5f; b->y = gy + 0.8f; b->z = gz + sz * 1.5f;
                b->vx = sx * 2.0f; b->vz = sz * 2.0f; b->vy = 0.35f;
                b->life = 24.f + 6.f * rnd_f(&rng_); b->r0 = 2.0f * gs; b->r1 = (11.f + 3.f * rnd_f(&rng_)) * gs;
                b->dens = 0.40f; b->shade = 0.52f; b->seed = rnd_f(&rng_); b->kind = 2; }
        }
        // layer 1 - the core: four dense cumulus balls, fast, slow to thin, big enough to
        // swallow the gun deck the way the reference frame's do. 0.22.9 (issue6): powder
        // smoke is grey, not white - the shade comes down and the count goes up, and each
        // ball is thrown off the gun's own line so the cloud is never a tidy row of spheres.
        for (int k = 0; k < (vol_on_ ? 0 : 6); ++k) {   // 0.22.14 (issue11 "more dense smoke, grey and white"): six, whiter, longer; 0.22.15: the billboards only without the volume
            ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_CORE;
            float spd = (k == 0 ? 14.f : 10.f - 2.f * k) * (0.85f + 0.3f * rnd_f(&rng_));
            float jx = (rnd_f(&rng_) - 0.5f), jz = (rnd_f(&rng_) - 0.5f);
            float off = (rnd_f(&rng_) - 0.5f) * 0.9f * gs;
            p->x = gx + sx * (0.7f + k * 0.55f) + fx * off; p->y = gy + (rnd_f(&rng_) - 0.3f) * 0.4f + 0.28f * k; p->z = gz + sz * (0.7f + k * 0.55f) + fz * off;
            p->vx = sx * spd + fx * jx * 2.4f; p->vz = sz * spd + fz * jz * 2.4f; p->vy = 0.6f + rnd_f(&rng_) * 0.9f;
            p->life = 6.0f + rnd_f(&rng_) * 3.0f + 0.6f * k;   // 0.22.12: longer in the air
            p->size0 = (0.5f + 0.25f * k) * gs; p->size1 = (4.8f + 0.9f * k + rnd_f(&rng_) * 1.4f) * gs;
            p->alpha = 1.0f; p->shade = 0.82f - 0.04f * k + 0.08f * rnd_f(&rng_);   // 0.22.14: white cores, greying as they thin   // 0.22.10: greyer again; 0.22.12: greyer still, opaque
            p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f) * 0.9f;
        }
        // layer 2 - the plume: four darker grey puffs trailing the core
        for (int k = 0; k < (vol_on_ ? 0 : 6); ++k) {   // 0.22.14: six grey plumes
            ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SMOKE;
            float spd = (8.f - 2.f * k) * (0.85f + 0.3f * rnd_f(&rng_));
            float jx = (rnd_f(&rng_) - 0.5f), jz = (rnd_f(&rng_) - 0.5f);
            p->x = gx + sx * (0.5f + k * 0.4f); p->y = gy + (rnd_f(&rng_) - 0.3f) * 0.3f; p->z = gz + sz * (0.5f + k * 0.4f);
            p->vx = sx * spd + jx * 1.2f + fx * jx * 1.5f; p->vz = sz * spd + jz * 1.2f + fz * jz * 1.5f; p->vy = 0.4f + rnd_f(&rng_) * 0.8f;
            p->life = 8.f + rnd_f(&rng_) * 5.f + k * 0.8f;
            p->size0 = (0.35f + 0.15f * k) * gs; p->size1 = (4.4f + 1.4f * k + rnd_f(&rng_) * 1.6f) * gs;
            p->alpha = 0.98f; p->shade = 0.52f - 0.04f * k + 0.08f * rnd_f(&rng_);   // 0.22.14: denser, a cleaner grey
            p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f) * 0.6f;
        }
        // layer 3 - the haze: one big thin veil that hangs about and drifts off on the wind
        if (!vol_on_) { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_HAZE;
          p->x = gx + sx * 1.5f; p->y = gy + 0.6f; p->z = gz + sz * 1.5f;
          p->vx = sx * 2.5f; p->vz = sz * 2.5f; p->vy = 0.25f;
          p->life = 16.f + rnd_f(&rng_) * 8.f;
          p->size0 = 1.5f * gs; p->size1 = (9.f + rnd_f(&rng_) * 4.f) * gs;
          p->alpha = 0.75f; p->shade = 0.48f; p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f) * 0.2f; }
        // embers: burning wad thrown with the shot, out in a third of a second
        int ne = 0;   // 0.22.14 (issue11 "remove ugly particles"): the orange ember dots are gone; the fire sheet on the flash carries the sparks. (was 5 + 4 gs)
        for (int k = 0; k < ne; ++k) {
            ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_EMBER;
            float spd = 10.f + rnd_f(&rng_) * 9.f, up = (rnd_f(&rng_) - 0.3f) * 3.f, sw = (rnd_f(&rng_) - 0.5f) * 3.f;
            p->x = gx + sx * 0.8f; p->y = gy; p->z = gz + sz * 0.8f;
            p->vx = sx * spd + fx * sw; p->vz = sz * spd + fz * sw; p->vy = up;
            p->life = 0.25f + rnd_f(&rng_) * 0.3f; p->size0 = 0.09f * gs; p->size1 = 0.04f * gs; p->alpha = 1.f; p->seed = rnd_f(&rng_);
        }
        // the water's answer: the blast of a gun a few metres over the sea flattens a ring of it
        // and throws a sheet of spray outboard (the reference frame's splashes along the side)
        if (0 && deck < 3.2f * gs && g.ring) {   // 0.22.14: the spout under the guns is off (issue11 "ugly particles") - the sea's own blast ring answers the gun
            float wx = gx + sx * (2.0f + 0.8f * gs), wz = gz + sz * (2.0f + 0.8f * gs);
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
        // the air's answer: one shockwave per six guns, light enough to read as heat shimmer
        // over the sea rather than a lens laid on the frame (0.22.9, "more transparent")
        if (g.ring && (rnd_f(&rng_) < 0.34f)) shock(gx + sx * 1.2f, gy, gz + sz * 1.2f, (4.f + 2.5f * gs), clampf(0.18f + 0.14f * gs, 0.18f, 0.40f));
}

// A round shot into the sea (0.22.11): a column of spray that climbs and falls, with a ring
// of foam on the water where it went in. `size` is the shot's weight, roughly the gun factor.
void Particles::splash(float x, float z, float size) {
    int ns = 6 + (int)(size * 6.f);
    for (int k = 0; k < ns; ++k) {
        ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SPRAY;
        float a = rnd_f(&rng_) * 6.2832f, rr = rnd_f(&rng_) * 0.5f * size;
        p->x = x + cosf(a) * rr; p->y = 0.1f; p->z = z + sinf(a) * rr;
        p->vx = cosf(a) * (0.6f + rnd_f(&rng_) * 1.2f); p->vz = sinf(a) * (0.6f + rnd_f(&rng_) * 1.2f);
        p->vy = 5.f + rnd_f(&rng_) * 7.f * size;
        p->life = 1.1f + rnd_f(&rng_) * 0.6f; p->size0 = 0.35f * size; p->size1 = 1.4f * size; p->alpha = 0.85f; p->seed = rnd_f(&rng_);
    }
    { ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_RING; p->x = x; p->y = 0.05f; p->z = z;
      p->life = 1.6f; p->size0 = 0.3f * size; p->size1 = 2.6f * size; p->alpha = 0.55f; p->seed = rnd_f(&rng_); }
}

// A round shot into a hull (0.22.11): a burst of splinters thrown on through the planking,
// a puff of dust and smoke, and a few embers where the wad went in.
void Particles::hit(float x, float y, float z, float dx, float dz, float size) {
    int nsp = 8 + (int)(size * 6.f);
    for (int k = 0; k < nsp; ++k) {
        ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SPLINTER;
        float sw = (rnd_f(&rng_) - 0.5f) * 1.4f, up = rnd_f(&rng_) * 4.f;
        p->x = x; p->y = y + (rnd_f(&rng_) - 0.3f) * 0.4f; p->z = z;
        p->vx = dx * (4.f + rnd_f(&rng_) * 7.f) - dz * sw * 3.f; p->vz = dz * (4.f + rnd_f(&rng_) * 7.f) + dx * sw * 3.f; p->vy = up;
        p->life = 0.7f + rnd_f(&rng_) * 0.8f; p->size0 = 0.12f * size; p->size1 = 0.10f * size; p->alpha = 1.f; p->seed = rnd_f(&rng_);
    }
    for (int k = 0; k < 2; ++k) {
        ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SMOKE;
        p->x = x + dx * 0.3f; p->y = y + 0.2f; p->z = z + dz * 0.3f;
        p->vx = dx * 1.5f + (rnd_f(&rng_) - 0.5f); p->vz = dz * 1.5f + (rnd_f(&rng_) - 0.5f); p->vy = 0.8f;
        p->life = 2.2f + rnd_f(&rng_); p->size0 = 0.3f * size; p->size1 = 1.8f * size; p->alpha = 0.7f; p->shade = 0.48f + 0.1f * k;
        p->seed = rnd_f(&rng_); p->rot = rnd_f(&rng_) * 6.28f; p->rotv = (rnd_f(&rng_) - 0.5f);
    }
    for (int k = 0; k < 3; ++k) {
        ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_EMBER;
        p->x = x; p->y = y; p->z = z; p->vx = dx * 3.f + (rnd_f(&rng_) - 0.5f) * 2.f; p->vz = dz * 3.f + (rnd_f(&rng_) - 0.5f) * 2.f; p->vy = 1.f + rnd_f(&rng_) * 2.f;
        p->life = 0.3f + rnd_f(&rng_) * 0.3f; p->size0 = 0.08f * size; p->size1 = 0.04f * size; p->alpha = 1.f; p->seed = rnd_f(&rng_);
    }
}

// Spray blown off a bow in a fresh breeze (0.22.11, todo 19 "weather effects in battle"):
// a few drops torn off the stem and carried downwind over the deck.
void Particles::spray(float x, float y, float z, float dx, float dz, float strength) {
    int n = 1 + (int)(strength * 2.f);
    for (int k = 0; k < n; ++k) {
        ptc_t *p = spawn(); *p = ptc_t{}; p->kind = PK_SPRAY;
        p->x = x + (rnd_f(&rng_) - 0.5f) * 0.6f; p->y = y; p->z = z + (rnd_f(&rng_) - 0.5f) * 0.6f;
        p->vx = dx * (3.f + rnd_f(&rng_) * 3.f) * strength; p->vz = dz * (3.f + rnd_f(&rng_) * 3.f) * strength; p->vy = 1.5f + rnd_f(&rng_) * 1.5f;
        p->life = 0.6f + rnd_f(&rng_) * 0.4f; p->size0 = 0.25f; p->size1 = 0.9f; p->alpha = 0.55f * strength; p->seed = rnd_f(&rng_);
    }
}

// The rings, to the sea (render.h sea_set_blasts): the front runs out and slows, the ring
// broadens and thins, and it is gone in a couple of seconds. Pushed from update(), which runs
// before the water draws - draw() happens after it, with the translucent passes.
void Particles::push_blasts() {
    float a[SEA_MAX_BLASTS * 4], d[SEA_MAX_BLASTS * 4]; int n = 0;
    for (int i = 0; i < nblast_ && n < SEA_MAX_BLASTS; ++i) { const blast_t *b = &bl_[i];
        float t = b->age / b->life, ease = 1.f - (1.f - t) * (1.f - t);
        float R = b->inner + (b->rmax - b->inner) * ease, str = b->strength * (1.f - t) * (1.f - t);
        if (str < 0.01f) continue;
        a[n*4] = b->x; a[n*4+1] = b->z; a[n*4+2] = R; a[n*4+3] = str;
        d[n*4] = b->dx; d[n*4+1] = b->dz; d[n*4+2] = b->inner; d[n*4+3] = 0.35f * b->inner + 0.5f * R * t; n++; }
    sea_set_blasts(a, d, n);
}

void Particles::update(float dt) {
    if (dt <= 0.f) { push_blasts(); return; }   // a stopped battle keeps the rings it has
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
                // 0.22.12: the cloud is carried at half the wind - a heavy bank of powder smoke
                // lags the air it hangs in - and rises more slowly, so it stays over the fight
                p->vx += (wind_x_ * 0.5f - p->vx) * k; p->vz += (wind_z_ * 0.5f - p->vz) * k;
                float buoy = (p->kind == PK_CORE ? 0.9f : 0.7f) * (1.f - t) * (1.f - t) + 0.06f;
                p->vy += (buoy - p->vy) * (1.f - expf(-dt * 1.2f));
                p->rot += p->rotv * dt;
            } break;
            case PK_FLASH: p->vx *= expf(-dt * 8.f); p->vz *= expf(-dt * 8.f); break;
            case PK_EMBER: p->vy -= G * dt; { float d = expf(-dt * 2.5f); p->vx = wind_x_ + (p->vx - wind_x_) * d; p->vz = wind_z_ + (p->vz - wind_z_) * d; } break;
            case PK_SPRAY: p->vy -= G * 0.8f * dt; { float d = expf(-dt * 1.5f); p->vx *= d; p->vz *= d; } break;
            case PK_SPLINTER: p->vy -= G * dt; { float d = expf(-dt * 0.8f); p->vx *= d; p->vz *= d; } p->rot += 7.f * dt; break;
            case PK_RING: break;
        }
        p->x += p->vx * dt; p->y += p->vy * dt; p->z += p->vz * dt;
        if ((p->kind == PK_EMBER || p->kind == PK_SPRAY || p->kind == PK_SPLINTER) && p->y < 0.02f) { *p = p_[--n_]; continue; }   // quenched / back in the sea
        ++i;
    }
    static int freeze = -1; if (freeze < 0) { const char *e = getenv("MARE_VOLSMOKE_DEBUG"); freeze = e && atoi(e) == 5 ? 1 : 0; }   // =5: the blobs never age (a screenshot always holds smoke)
    for (int i = 0; i < nblob_; ) {   // the volume: drag towards half the wind, a little buoyancy, gone at its life
        blob_t *b = &vb_[i]; if (!freeze || b->age < 0.35f * b->life) b->age += dt;
        if (b->age >= b->life) { *b = vb_[--nblob_]; continue; }
        float t = b->age / b->life, d = expf(-dt * 0.55f);
        b->vx = wind_x_ * 0.5f + (b->vx - wind_x_ * 0.5f) * d; b->vz = wind_z_ * 0.5f + (b->vz - wind_z_ * 0.5f) * d;
        b->vy = b->vy * expf(-dt * 0.8f) + (0.45f * (1.f - t) * (1.f - t) + 0.04f) * dt;
        b->x += b->vx * dt; b->y += b->vy * dt; b->z += b->vz * dt;
        ++i;
    }
    for (int i = 0; i < nshock_; ) { sh_[i].age += dt; if (sh_[i].age >= sh_[i].life) sh_[i] = sh_[--nshock_]; else ++i; }
    for (int i = 0; i < nblast_; ) { bl_[i].age += dt; if (bl_[i].age >= bl_[i].life) bl_[i] = bl_[--nblast_]; else ++i; }
    for (int i = 0; i < ngun_; ) { gq_[i].t -= dt; if (gq_[i].t <= 0.f) { gun_t g = gq_[i]; gq_[i] = gq_[--ngun_]; fire_gun(g); } else ++i; }
    push_blasts();
}

void Particles::vol_draw() {
    if (!vol_inited_) return;
    float b4[BLOB_MAX * 4], p4[BLOB_MAX * 4];
    for (int i = 0; i < nblob_; ++i) { const blob_t *b = &vb_[i];
        float t = clampf(b->age / maxf(b->life, 0.01f), 0.f, 1.f), g = 1.f - (1.f - t) * (1.f - t) * (1.f - t);   // fast early growth
        float R = lerpf(b->r0, b->r1, g);
        float dens = b->dens * (1.f - t * t) * (b->kind == 0 ? 1.f : 1.f) / maxf(R / b->r1, 0.15f);   // the same smoke in a bigger ball is thinner
        b4[i*4] = b->x; b4[i*4+1] = b->y; b4[i*4+2] = b->z; b4[i*4+3] = R;
        p4[i*4] = dens; p4[i*4+1] = t; p4[i*4+2] = b->seed; p4[i*4+3] = lerpf(b->shade, 0.55f, t * 0.7f); }
    rhi_pipeline_bind(&vol_prg_);
    rhi_set_mat4(UNI(vol_prg_, "u_inv_vp"), &cam.inv_vp.x.x);
    rhi_set_f3(UNI(vol_prg_, "u_campos"), RF.campos.x, RF.campos.y, RF.campos.z);
    rhi_set_f3(UNI(vol_prg_, "u_sky"), RF.sky.x, RF.sky.y, RF.sky.z);
    rhi_set_f1(UNI(vol_prg_, "u_mapness"), RF.mapness);
    rhi_set_f1(UNI(vol_prg_, "u_time"), RF.anim_time);
    rhi_set_f3(UNI(vol_prg_, "u_wind"), wind_x_, 0.f, wind_z_);
    rhi_set_f4v(UNI(vol_prg_, "u_blob"), b4, nblob_);
    rhi_set_f4v(UNI(vol_prg_, "u_blobp"), p4, nblob_);
    rhi_set_i1(UNI(vol_prg_, "u_nblob"), nblob_);
    { static int dbg = -1; if (dbg < 0) { const char *e = getenv("MARE_VOLSMOKE_DEBUG"); dbg = e ? atoi(e) : 0; } rhi_set_f1(UNI(vol_prg_, "u_debug"), (float)dbg); }
    { static double last_log; if (RF.time - last_log > 2.0) { last_log = RF.time; LOG_DEBUG("gfx", "volsmoke: %d blobs, first at %.1f %.1f %.1f r %.2f dens %.2f, cam %.1f %.1f %.1f", nblob_, b4[0], b4[1], b4[2], b4[3], p4[0], RF.campos.x, RF.campos.y, RF.campos.z); } }
    { const rhi_texture_t *dp = hdr_depth_captured();
      if (dp) { rhi_set_i1(UNI(vol_prg_, "u_scene_depth"), 7); rhi_texture_bind(7, dp); rhi_set_f1(UNI(vol_prg_, "u_soft_on"), 1.f); }
      else rhi_set_f1(UNI(vol_prg_, "u_soft_on"), 0.f); }
    light_bind(&vol_prg_);
    rhi_draw(&vol_tri_, 0);
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
    if (vol_on_ && nblob_) vol_draw();   // the volumetric smoke (0.22.15)
    if (n_ == 0) return nblob_;
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
            case PK_SMOKE: { float a = p->alpha * powf(1.f - t, 0.9f) * clampf(p->age / 0.08f, 0.f, 1.f);   // 0.22.14: holds its density longer
                             float sh = lerpf(p->shade, 0.62f, t);   // greys as it thins
                             alpha = a; r = sh; g = sh * 0.99f; b = sh * 0.98f; } break;
            case PK_CORE:  { float a = p->alpha * powf(1.f - t, 0.7f) * clampf(p->age / 0.05f, 0.f, 1.f);
                             float sh = lerpf(p->shade, 0.60f, t);   // 0.22.14: white at birth, grey as it thins
                             alpha = a; r = sh; g = sh; b = sh * 0.99f; } break;
            case PK_HAZE:  { float a = p->alpha * sinf(3.14159f * minf(t, 1.f)) * clampf(p->age / 0.4f, 0.f, 1.f);
                             alpha = a; r = p->shade; g = p->shade; b = p->shade * 1.01f; } break;
            case PK_FLASH: alpha = 1.f - t * t; r = 1.0f; g = 0.78f; b = 0.42f; break;
            case PK_EMBER: alpha = 1.f - t; r = 1.0f; g = 0.55f + 0.3f * (1.f - t); b = 0.18f; break;
            case PK_SPRAY: alpha = p->alpha * (1.f - t * t); r = 0.92f; g = 0.95f; b = 0.97f; break;
            case PK_RING:  alpha = p->alpha * (1.f - t) * (1.f - t) * clampf(p->age / 0.05f, 0.f, 1.f); r = 0.90f; g = 0.93f; b = 0.95f; break;
            case PK_SPLINTER: alpha = 1.f - t * t; r = 0.55f; g = 0.42f; b = 0.26f; break;
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
    {   // the sprite atlases (units 8-10; the shadow map is on 3, the scene depth on 7)
        const rhi_texture_t *ts = tex_smoke_ >= 0 ? texture_get(tex_smoke_) : 0, *tf = tex_fire_ >= 0 ? texture_get(tex_fire_) : 0, *tw = tex_foam_ >= 0 ? texture_get(tex_foam_) : 0;
        if (ts) { rhi_set_i1(UNI(prg_, "u_fx_smoke"), 8); rhi_texture_bind(8, ts); }
        if (tf) { rhi_set_i1(UNI(prg_, "u_fx_fire"), 9);  rhi_texture_bind(9, tf); }
        if (tw) { rhi_set_i1(UNI(prg_, "u_fx_foam"), 10); rhi_texture_bind(10, tw); }
        rhi_set_f3(UNI(prg_, "u_has_fx"), ts ? 1.f : 0.f, tf ? 1.f : 0.f, tw ? 1.f : 0.f);
    }
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
void particles_broadside(float x, float y, float z, float heading, float side, float hull_len, int guns, int body) { mare::engine::render::particles().broadside(x, y, z, heading, side, hull_len, guns, body); }
void particles_splash(float x, float z, float size) { mare::engine::render::particles().splash(x, z, size); }
void particles_hit(float x, float y, float z, float dx, float dz, float size) { mare::engine::render::particles().hit(x, y, z, dx, dz, size); }
void particles_spray(float x, float y, float z, float dx, float dz, float strength) { mare::engine::render::particles().spray(x, y, z, dx, dz, strength); }
void particles_update(float dt) { mare::engine::render::particles().update(dt); }
int  particles_draw(void) { return mare::engine::render::particles().draw(); }
int  particles_count(void) { return mare::engine::render::particles().count(); }
}
