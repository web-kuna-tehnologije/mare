// World LOD selection (game/docs/DESIGN_LOD.md): one screen-space metric for everything that
// has levels of detail - authored assets, procedural ships and towns, and the mesh-to-icon
// hand-over - with per-instance hysteresis and a timed cross-fade so nothing pops.
//
// The metric is the projected diameter in pixels, px = 2 r k / d (k from the vertical FOV and
// the framebuffer height). Thresholds are pixel sizes too: authored switch distances are turned
// into pixels for a reference 1080p view at load, so the same model keeps the same on-screen
// detail at 4K, at 720p, and when the FOV or the map morph changes the projection. The bias
// knob (asset_lod_bias) scales the thresholds exactly as it scaled distances before.
//
// Per-instance state is kept in a fixed hash table keyed by a stable id (port index, fleet id,
// town piece). It carries the current level, the level being faded towards and the fade clock.
// Selection has hysteresis (LOD_HYST) so a camera hovering on a boundary does not flicker, and
// a change plays as a LOD_FADE_S cross-fade: both levels are drawn with complementary dithering
// (inst_t.dither, see gpu.h) for that long. Levels past the last mesh (the icon) use the same
// machinery; lod_weight() gives the icon pass its alpha.
//
// This file depends on nothing but math, so the headless tests can run the selector.

#ifndef MARE_LOD_H
#define MARE_LOD_H

#define LOD_HYST    0.12f     // +-12% around a threshold before a level change is accepted
#define LOD_FADE_S  0.30f     // seconds a cross-fade lasts
#define LOD_MAX_LEVELS 7      // meshes (ASSET_MAX_LODS) + icon

// px thresholds calibrated against the old world-distance rules at 1080p / 45 degrees:
// a size-2 town (r 3.5) became an icon at 260 units = 35 px, a lone sloop's fleet (r 2.4) went
// simplified at 75 units (83 px) and icon at 230 (27 px). Pixels now, so a great city stays 3D
// further out than a hamlet and a 12-ship fleet further than a single sloop.
#define LOD_PX_SHIP_SIMPLE 80.f
#define LOD_PX_SHIP_ICON   26.f
#define LOD_PX_TOWN_ICON   35.f

enum { LODK_PORT = 1, LODK_FLEET, LODK_TOWN_ITEM, LODK_HINTERLAND, LODK_MISC };

// View tiers (DESIGN_LOD.md step 5). The tier is a property of the camera, not of an object:
// it says what kind of scene this is, and carries the triangle budget the director holds the
// frame to. The thresholds are the ones the game already used - city view, the full-mesh ship
// range, the town range, the map morph - given names and a policy.
enum { TIER_CITY, TIER_BATTLE, TIER_REGIONAL, TIER_STRATEGIC, TIER_CONTINENTAL, TIER_COUNT };
int         lod_tier(void);
const char *lod_tier_name(int tier);
long long   lod_tier_budget(int tier);          // authored-asset triangles the tier allows per frame

// The director closes the loop: the scene build reports what it actually submitted, and the
// thresholds for the next frame are scaled so the budget is met. Quality degrades at the
// horizon (the smallest instances drop a level first) rather than in the middle of the screen,
// and it recovers as soon as there is room again.
void  lod_budget_report(long long asset_tris);
extern float lod_budget_scale;                  // >1 = detail levels hand over sooner
extern long long lod_last_tris;                 // what the previous frame submitted (dashboard)

typedef struct lod_pick_t {
    int   lod;          // level to draw (the one being faded out when fading)
    int   lod2;         // level being faded in, or -1
    float t;            // fade progress 0..1 (0 when not fading)
} lod_pick_t;

void  lod_frame_begin(float screen_h, float fov_rad, float dt);   // once per frame, before any lod_select
void  lod_set_tier(float cam_dist, float mapness);                // with it: which kind of scene this frame is
float lod_px(float radius, float dist);                           // projected diameter in pixels this frame
float lod_px_ref(float radius, float dist);                       // the same for the reference 1080p view (thresholds from authored distances)
unsigned lod_key(int kind, int id, int sub);                      // stable per-instance key

// Select among `n` levels. thr[i] (i < n-1) is the pixel size below which level i+1 is used;
// thresholds decrease with i. Returns the level(s) to draw and the fade state.
// `nbudget` thresholds (from the finest end) are scaled by the director's budget. Pass 0 to opt
// out: a mesh-to-icon hand-over is never budgeted, or a town would blink out under load.
lod_pick_t lod_select(unsigned key, const float *thr, int n, float px, int nbudget);
// Helpers to turn a pick into draw parameters:
float lod_dither(const lod_pick_t *p, int level);   // inst_t.dither for drawing `level`: 0 opaque, +t fading out, -t fading in
float lod_weight(const lod_pick_t *p, int level);   // 0..1 visibility of `level` (icon alpha)
int   lod_draws(const lod_pick_t *p, int level);    // 1 if `level` is one of the picked levels

// statistics (dashboard / metrics): reset by lod_frame_begin
extern int lod_pops;        // multi-level jumps the one-step clamp had to hold back (a visible pop cannot occur)
extern int lod_fading;      // instances mid-fade this frame
extern int lod_tracked;     // live entries in the state table
void lod_reset(void);       // forget every instance (new game / load)

#endif
