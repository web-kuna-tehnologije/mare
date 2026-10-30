// mare::engine::render::Lod (see lod.h): LOD selection. Pure math and a hash table: no GL, no
// camera, so asset_test can drive it headlessly.
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "lod.h"
#include "../core/diag.h"
}

#define LOD_TABLE 16384       // power of two; ~6k ports+fleets+town pieces on screen at once is far above anything the game draws

int lod_pops, lod_fading, lod_tracked;
float lod_budget_scale = 1.f;
long long lod_last_tris;

namespace mare::engine::render {

#define LOD_K_FOR(h, fov) ((h) / (2.f * tanf((fov) * 0.5f)))

class Lod {
public:
    int         tier() { return cur_tier; }
    const char *tier_name(int t);
    long long   tier_budget_of(int t);          // lod_tier_budget (the array below keeps the C name)
    void  set_tier(float cam_dist, float mapness);
    void  budget_report(long long asset_tris);
    void  frame_begin(float screen_h, float fov_rad, float dt);
    void  reset();
    float pixels(float radius, float dist);
    float pixels_ref(float radius, float dist);
    unsigned key(int kind, int id, int sub);
    lod_pick_t select(unsigned key, const float *thr, int n, float px, int nbudget);
    float dither(const lod_pick_t *p, int level);
    float weight(const lod_pick_t *p, int level);
    int   draws(const lod_pick_t *p, int level);

private:
    typedef struct lod_state_t { unsigned key; unsigned frame; unsigned char cur, target; float t; } lod_state_t;
    lod_state_t table[LOD_TABLE];
    unsigned frame_no = 1;
    float lod_k = 0, lod_k_ref = 0, lod_dt = 0;
    int cur_tier = TIER_REGIONAL;

    // Tier thresholds are the camera distances the game already switched behaviour at (camera.h,
    // scene.h); the budgets are triangle counts for authored assets, measured on the integrated GPU
    // this is developed against - a city at 2.5M triangles holds 30 fps there, the map needs far
    // less because nothing near the camera is a building.
    static constexpr long long tier_budget[TIER_COUNT] = { 2500000, 2200000, 1400000, 400000, 0 };
    static constexpr const char *tier_names[TIER_COUNT] = { "city", "battle", "regional", "strategic", "continental" };

    static int lod_raw(const float *thr, int n, float px);
    lod_state_t *lod_slot(unsigned key);
};

Lod &lod() { static Lod l; return l; }

const char *Lod::tier_name(int t) { return (unsigned)t < TIER_COUNT ? tier_names[t] : "?"; }
long long   Lod::tier_budget_of(int t) { return (unsigned)t < TIER_COUNT ? tier_budget[t] : 0; }

void Lod::set_tier(float cam_dist, float mapness) {
    cur_tier = mapness > 0.98f ? TIER_CONTINENTAL
             : cam_dist > 260.f ? TIER_STRATEGIC
             : cam_dist > 75.f  ? TIER_REGIONAL
             : cam_dist > 16.f  ? TIER_BATTLE : TIER_CITY;
}

// Feedback, not a sort: scaling the thresholds costs nothing and settles within a few frames,
// where ranking every instance by projected size would cost more than the triangles it saves.
void Lod::budget_report(long long asset_tris) {
    lod_last_tris = asset_tris;
    long long budget = tier_budget[cur_tier];
    if (budget <= 0) { lod_budget_scale = 1.f; return; }
    if (asset_tris > budget)            lod_budget_scale *= 1.05f;
    else if (asset_tris < budget * 8/10) lod_budget_scale *= 0.97f;
    if (lod_budget_scale < 1.f) lod_budget_scale = 1.f;
    if (lod_budget_scale > 6.f) lod_budget_scale = 6.f;
}

void Lod::frame_begin(float screen_h, float fov_rad, float dt) {
    lod_k = LOD_K_FOR(screen_h, fov_rad);
    lod_k_ref = LOD_K_FOR(1080.f, 45.f * 0.0174533f);
    lod_dt = dt < 0 ? 0 : dt > 0.25f ? 0.25f : dt;
    frame_no++;
    lod_pops = lod_fading = 0;
}
void Lod::reset() { memset(table, 0, sizeof table); lod_tracked = 0; lod_budget_scale = 1.f; }

float Lod::pixels(float radius, float dist)     { return dist > 1e-3f ? 2.f * radius * lod_k / dist : 1e9f; }
float Lod::pixels_ref(float radius, float dist) { return dist > 1e-3f ? 2.f * radius * lod_k_ref / dist : 1e9f; }

unsigned Lod::key(int kind, int id, int sub) {
    unsigned h = (unsigned)kind * 0x9E3779B1u ^ (unsigned)id * 0x85EBCA6Bu ^ (unsigned)sub * 0xC2B2AE35u;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
    return h ? h : 1;                      // 0 marks an empty slot
}

// stateless choice: the last level whose threshold px is still below
int Lod::lod_raw(const float *thr, int n, float px) {
    int l = 0;
    while (l + 1 < n && px < thr[l]) l++;
    return l;
}

Lod::lod_state_t *Lod::lod_slot(unsigned key) {
    unsigned i = key & (LOD_TABLE - 1);
    lod_state_t *victim = 0;
    for (int probe = 0; probe < 32; ++probe, i = (i + 1) & (LOD_TABLE - 1)) {
        lod_state_t *s = &table[i];
        if (s->key == key) return s;
        if (!s->key) { if (!victim) victim = s; break; }
        if (!victim && frame_no - s->frame > 120) victim = s;   // not seen for ~2 s: reusable
    }
    if (!victim) return 0;
    if (!victim->key) lod_tracked++;
    victim->key = key; victim->frame = 0; victim->cur = victim->target = 255; victim->t = 0;
    return victim;
}

lod_pick_t Lod::select(unsigned key, const float *thr, int n, float px, int nbudget) {
    lod_pick_t p = { 0, -1, 0 };
    if (n < 1) return p;
    if (n > LOD_MAX_LEVELS) n = LOD_MAX_LEVELS;
    float scaled[LOD_MAX_LEVELS];
    if (nbudget > 0 && lod_budget_scale != 1.f) {
        for (int i = 0; i < n - 1; ++i) scaled[i] = i < nbudget ? thr[i] * lod_budget_scale : thr[i];
        thr = scaled;
    }
    int raw = lod_raw(thr, n, px);
    lod_state_t *s = lod_slot(key);
    if (!s) { p.lod = raw; return p; }                 // table saturated: stateless fallback
    if (s->cur == 255 || s->frame == 0) { s->cur = s->target = (unsigned char)raw; s->t = 0; }   // first sight: no fade in from nowhere
    s->frame = frame_no;
    if (s->cur >= n) s->cur = (unsigned char)(n - 1);
    if (s->target >= n) s->target = (unsigned char)(n - 1);

    // hysteresis around the *displayed* level: move only once px has crossed the threshold by
    // the margin, in whichever direction
    int want = s->target;
    while (want + 1 < n && px < thr[want] * (1.f - LOD_HYST)) want++;
    while (want > 0 && px > thr[want - 1] * (1.f + LOD_HYST)) want--;
    // Never move more than one level per change, however far the camera (or the director's
    // budget scale) jumped: the instance walks the levels one cross-fade at a time instead of
    // snapping two steps at once. This is what keeps a visible pop impossible rather than
    // merely unlikely; the counter below records how often the clamp had to act.
    if (want > s->cur + 1) { want = s->cur + 1; lod_pops++; }
    else if (want < s->cur - 1) { want = s->cur - 1; lod_pops++; }

    if (want != s->target) {
        if (s->target != s->cur && want == s->cur) {                         // reversed mid-fade: swap roles, play it back
            unsigned char o = s->cur; s->cur = s->target; s->target = o; s->t = 1.f - s->t;
        } else {
            if (s->target != s->cur) { s->cur = s->target; s->t = 0; }       // retargeted mid-fade: commit the one being faded in
            s->target = (unsigned char)want;
            if (s->target != s->cur) { s->t = 1e-4f;                         // fade starts
                LOG_TRACE("lod", "key %08x: %d -> %d at %.1f px (thr %.1f / %.1f)", key, s->cur, want, px, thr[0], n > 2 ? thr[1] : 0.f); }
        }
    }
    if (s->target != s->cur) {
        s->t += LOD_FADE_S > 0 ? lod_dt / LOD_FADE_S : 1.f;
        if (s->t >= 1.f) { s->cur = s->target; s->t = 0; }
        else lod_fading++;
    }
    p.lod = s->cur;
    p.lod2 = s->target != s->cur ? s->target : -1;
    p.t = p.lod2 >= 0 ? s->t : 0;
    return p;
}

float Lod::dither(const lod_pick_t *p, int level) {
    if (p->lod2 < 0) return 0;
    if (level == p->lod)  return p->t;          // fading out: keep (1 - t)
    if (level == p->lod2) return -p->t;         // fading in: keep t
    return 0;
}
float Lod::weight(const lod_pick_t *p, int level) {
    if (p->lod2 < 0) return level == p->lod ? 1.f : 0.f;
    if (level == p->lod)  return 1.f - p->t;
    if (level == p->lod2) return p->t;
    return 0;
}
int Lod::draws(const lod_pick_t *p, int level) { return level == p->lod || level == p->lod2; }

} // namespace mare::engine::render

// C ABI (lod.h) for the unported callers
extern "C" {
int         lod_tier(void) { return mare::engine::render::lod().tier(); }
const char *lod_tier_name(int t) { return mare::engine::render::lod().tier_name(t); }
long long   lod_tier_budget(int t) { return mare::engine::render::lod().tier_budget_of(t); }
void  lod_set_tier(float cam_dist, float mapness) { mare::engine::render::lod().set_tier(cam_dist, mapness); }
void  lod_budget_report(long long asset_tris) { mare::engine::render::lod().budget_report(asset_tris); }
void  lod_frame_begin(float screen_h, float fov_rad, float dt) { mare::engine::render::lod().frame_begin(screen_h, fov_rad, dt); }
void  lod_reset(void) { mare::engine::render::lod().reset(); }
float lod_px(float radius, float dist) { return mare::engine::render::lod().pixels(radius, dist); }
float lod_px_ref(float radius, float dist) { return mare::engine::render::lod().pixels_ref(radius, dist); }
unsigned lod_key(int kind, int id, int sub) { return mare::engine::render::lod().key(kind, id, sub); }
lod_pick_t lod_select(unsigned key, const float *thr, int n, float px, int nbudget) { return mare::engine::render::lod().select(key, thr, n, px, nbudget); }
float lod_dither(const lod_pick_t *p, int level) { return mare::engine::render::lod().dither(p, level); }
float lod_weight(const lod_pick_t *p, int level) { return mare::engine::render::lod().weight(p, level); }
int   lod_draws(const lod_pick_t *p, int level) { return mare::engine::render::lod().draws(p, level); }
}
