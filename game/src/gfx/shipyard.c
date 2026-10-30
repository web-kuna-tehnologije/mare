// Authored ship assembly (see shipyard.h, game/docs/DESIGN_SHIPS.md).
// Sections: the Gen I class table (rig layout + stats) · game ship type -> class · rigging,
// canvas and flags · submission through the authored-asset path.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "shipyard.h"
#include "render.h"
#include "scene.h"
#include "lod.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../assets/asset.h"
#include "../data/tables.h"
#include "../world/wind.h"

int shipyard_enabled = 1;
int shipyard_oars_drawn, shipyard_ships_drawn, shipyard_fallbacks, shipyard_rigged;

// World units per metre. The design sheets give every Gen I ship a length in metres; this
// factor turns them into the scale the game already drew ships at (the procedural sloop
// occupied ~0.95 units, which is a 21 m coastal trader here). tools/gen_asset_descriptors.py
// uses the same constant to size the imported assets, so the two never drift.
#define SHIP_M_PER_UNIT 0.045f

// Oars are a per-ship bank of instances (a 22-pair galiot adds 44), so they are only submitted
// once the ship is big enough on screen for an oar to read as one. 140 px is about the point
// where a hull is a recognisable ship rather than a shape - closer than the regional view,
// comfortably inside the battle and city views where oars matter.
#define OAR_MIN_PX 140.f
// How far the loom reaches inboard of the oar's own centre: a real sweep is pivoted about a
// third of its length from the inboard end, so the bank sits outboard of the gunwale with the
// looms buried in the hull rather than floating alongside it.
#define OAR_INBOARD 0.22f

#define SHIP_MAX_MASTS 4

// What hangs from a mast. The reference sheet (assets/fleet_sails.png) draws a square course
// with a topsail on the fore and main of every round ship, a third sail on the great carrack's
// main, one great course on a cog, and a lateen on every mizzen and on every galley mast.
enum { SAIL_NONE = 0, SAIL_SQUARE1, SAIL_SQUARE2, SAIL_SQUARE3, SAIL_LATEEN };

typedef struct ship_class_t {
    const char *name;
    const char *hull, *masts, *oars;    // asset roles ("" / NULL when the class has none)
    float length_m;                     // design sheet length overall (midpoint of the range)
    float draft;                        // waterline above the keel, fraction of hull height
    float deck;                         // deck above the keel, fraction of hull height
    float model_scale;                  // stretch on the model when a class stands in on another's hull
    // The rig. Meshy generates a rig as one object; tools/blender_split_masts.py cuts it into
    // `<masts>_m0` (foremast) .. `_m2` (mizzen), narrowed and each standing on its own origin,
    // so every mast can be placed at the station its sheet gives. `split` says the class has
    // the three; a single-mast model (cog, armed cog, Mediterranean lateen) only has `_m0`.
    // `mast_part[k]` is which of those stands at station k - a two-mast layout uses the main
    // and mizzen parts, a four-mast one stands the mizzen part twice.
    int   split;
    int   nmasts;
    float mast_at[SHIP_MAX_MASTS];      // station along the hull, fraction of length (+ = bow)
    float mast_h[SHIP_MAX_MASTS];       // height of each, relative to the fitted model
    signed char mast_part[SHIP_MAX_MASTS];
    signed char sail[SHIP_MAX_MASTS];   // SAIL_*
    // The per-type mast library (assets/ships/new_masts, 2026-10-30): one authored assembly
    // per mast *type* - pole, tops and square yards in the model - shared across classes and
    // stood at each class's stations. NULL falls back to the old per-class split parts
    // (`<masts>_m<k>`), so a tree without the new imports still rigs its ships. Lateen
    // stations get their yard separately: the lantina is its own asset so it can swing.
    const char *mast_lib[SHIP_MAX_MASTS];
    int   spritsail;                    // a bowsprit with a spritsail under it
    int   oar_pairs;                    // per side
    float oar_span;                     // bank length along the hull, fraction of length
    float beam_frac;                    // half-beam at the oar ports, fraction of length
    ship_stats_t st;
} ship_class_t;

// The Gen I fleet as drawn in assets/fleet_sails.png and the design sheets. Hull and mast
// models come in pairs from Meshy except the galiot, which borrows the generic Mediterranean
// lateen rig, and the great carrack, which sails on the war carrack's model stretched to its
// own length until its hull is generated.
//
//   stats: length, beam, draft (m), displacement (t), knots under sail, under oars, turn deg/s, guns, crew
static const ship_class_t classes[] = {
  { "coastal_trader", "coastal_trader_hull", "coastal_trader_masts", NULL,         21.f, .38f, .60f, 1.f,  1, 2,
    { .04f, -.33f },              { 1.f, .62f },             { 1, 2 },       { SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_single", "mast_mizzen" }, 0, 0, 0.f, .16f,
    { 21.f,  6.0f, 1.8f,   60.f, 5.0f, 0.f, 6.0f,  4,  15 } },
  { "cog",            "cog_hull",            "cog_masts",            NULL,         25.f, .38f, .60f, 1.f,  0, 1,
    { .02f },                     { 1.f },                   { 0 },          { SAIL_SQUARE1 }, { "mast_cog_single" },               0, 0, 0.f, .17f,
    { 25.f,  7.5f, 2.5f,  150.f, 4.5f, 0.f, 4.0f,  2,  20 } },
  { "armed_cog",      "armed_cog_hull",      "armed_cog_masts",      NULL,         32.f, .38f, .60f, 1.f,  0, 2,
    { .03f, -.33f },              { 1.f, .58f },             { 0, 0 },       { SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_two", "mast_mizzen" }, 0, 0, 0.f, .16f,
    { 32.f,  9.0f, 3.0f,  260.f, 4.5f, 0.f, 3.5f, 14,  70 } },
  { "hulk",           "hulk_hull",           "hulk_masts",           NULL,         35.f, .38f, .62f, 1.f,  1, 3,
    { .27f, -.01f, -.30f },       { .80f, 1.f, .62f },       { 0, 1, 2 },    { SAIL_SQUARE1, SAIL_SQUARE2, SAIL_SQUARE1 }, { "mast_cog_two", "mast_cog_two", "mast_cog_single" }, 0, 0, 0.f, .19f,
    { 35.f, 11.0f, 3.5f,  400.f, 4.0f, 0.f, 3.0f,  8,  50 } },
  { "carrack",        "carrack_hull",        "carrack_masts",        NULL,         38.f, .36f, .58f, 1.f,  1, 3,
    { .28f, -.01f, -.31f },       { .80f, 1.f, .68f },       { 0, 1, 2 },    { SAIL_SQUARE2, SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_two", "mast_carrack", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 38.f, 11.0f, 4.0f,  500.f, 5.0f, 0.f, 3.0f, 20,  90 } },
  { "war_carrack",    "war_carrack_hull",    "war_carrack_masts",    NULL,         47.f, .36f, .58f, 1.f,  1, 4,
    { .31f, -.03f, -.26f, -.42f },{ .82f, 1.f, .70f, .52f }, { 0, 1, 2, 2 }, { SAIL_SQUARE2, SAIL_SQUARE3, SAIL_LATEEN, SAIL_LATEEN }, { "mast_cog_two", "mast_carrack", "mast_mizzen", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 47.f, 13.0f, 5.0f, 1000.f, 4.5f, 0.f, 2.2f, 40, 250 } },
  { "great_carrack",  "war_carrack_hull",    "war_carrack_masts",    NULL,         56.f, .36f, .58f, 56.f / 47.f, 1, 4,
    { .31f, -.03f, -.26f, -.42f },{ .85f, 1.f, .72f, .54f }, { 0, 1, 2, 2 }, { SAIL_SQUARE2, SAIL_SQUARE3, SAIL_LATEEN, SAIL_LATEEN }, { "mast_cog_two", "mast_great_carrack", "mast_mizzen", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 56.f, 15.0f, 5.5f, 1500.f, 4.0f, 0.f, 1.8f, 74, 500 } },
  { "galiot",         "galiot_hull",         "mediterranean_masts",  "galley_oar", 28.f, .32f, .50f, 1.f,  0, 2,
    { .08f, -.30f },              { 1.f, .62f },             { 0, 0 },       { SAIL_LATEEN, SAIL_LATEEN }, { "mast_lateen_tall", "mast_lateen_tall" },  0, 22, .62f, .11f,
    { 28.f,  5.5f, 1.5f,  120.f, 5.0f, 6.5f, 5.0f,  6, 120 } },
  { "fusta",          "fusta_hull",          "fusta_masts",          "galley_oar", 25.f, .32f, .50f, 1.f,  1, 2,
    { .08f, -.30f },              { 1.f, .62f },             { 1, 2 },       { SAIL_LATEEN, SAIL_LATEEN }, { "mast_lateen_tall", "mast_lateen_tall" },  0, 20, .60f, .09f,
    { 25.f,  4.5f, 1.2f,   80.f, 5.5f, 7.0f, 6.0f,  4,  90 } },
};
#define NCLASSES ((int)(sizeof classes / sizeof classes[0]))

// ---------------------------------------------------------------------------
// Rigging, sails and colours (DESIGN_SHIPS.md section 3c)
//
// The generated models are bare: hulls and spars, no cordage and no canvas. Both are built
// here as one procedural mesh per class, in the ship's own frame normalised to its hull length
// (+x bow, +y up from the waterline, +z starboard), so a single instance - position, heading,
// scale - puts them on any ship of that class. They go through the instanced mesh pipeline, so
// they roll, pitch and ride the swell with the hull they belong to.
//
// Two meshes rather than one, because they want different instance flags: the cordage is rigid
// (`fx1` 0) and the canvas is cloth (`fx1` 1, with the wind in `fx2`/`fx3`, which the cloth
// model in meshes.c reads). Every canvas vertex carries a cloth code (gpu.h mb_cloth_code):
// how free it is - 0 where it is bent to its yard, 1 at the loose foot - and where it sits
// between the leeches, and the panel's half-width, so the shader can belly each sail by the
// wind through it, pin it to its spar and hold it at the foot.

#define RIG_ROPE_W   0.0035f        // rope half-width, fractions of hull length
#define RIG_YARD_W   0.0050f        // yard / bowsprit half-width
#define RIG_SAIL_OFF 0.015f         // canvas hangs this far forward of its mast (on the yard's fore side)
#define RIG_FLAG_LEN 0.085f
#define RIG_FLAG_H   0.042f
static const float rope_col[3] = { 0.10f, 0.085f, 0.075f };   // tarred hemp, nearly black
static const float spar_col[3] = { 0.36f, 0.27f, 0.17f };     // oiled oak
static const float sail_col[3] = { 0.86f, 0.83f, 0.74f };     // unbleached canvas
static const float nrm_x[3] = { 1, 0, 0 }, nrm_z[3] = { 0, 0, 1 };

typedef struct rig_cache_t { mesh_t rope, cloth; int built; unsigned gen; } rig_cache_t;
static rig_cache_t rig_cache[NCLASSES];
static instlist_t  rig_inst[NCLASSES], cloth_inst[NCLASSES];

// The lateen canvas as one shared unit mesh (mast height 1, mast axis at the origin),
// instanced per lateen station with the yard's swung heading, so sail and lantina turn as one
// piece about the mast. Built lazily on the render thread, like the class rigs.
static mesh_t     lateen_cloth_mesh; static int lateen_cloth_built;
static instlist_t lateen_cloth_inst;

// Square canvas as shared unit meshes (half-width 1, head at y = 0, hanging down), one per
// cut: the deep single course of a cog, the standard course, the narrower topsails. Instanced
// per (ship, station, layer) on the yard's braced heading, so canvas and the authored
// square_yard turn together about the mast - the lantina's model, applied to the square rig.
enum { SQ_DEEP = 0, SQ_COURSE, SQ_TOP, SQ_KINDS };
static const float sq_drop[SQ_KINDS] = { 1.67f, 1.05f, 1.28f };
static mesh_t     sq_cloth_mesh[SQ_KINDS]; static int sq_cloth_built;
static instlist_t sq_cloth_inst[SQ_KINDS];

// per sail code: layers as (head height, half-width) fractions of the mast height, and cut
typedef struct sq_layer_t { float top, hw; signed char kind; } sq_layer_t;
typedef struct sq_layout_t { int n; sq_layer_t l[3]; } sq_layout_t;
static const sq_layout_t sq_layout[5] = {
    { 0, { { 0 } } },                                                             /* SAIL_NONE */
    { 1, { { 0.80f, 0.30f, SQ_DEEP } } },                                         /* SAIL_SQUARE1 */
    { 2, { { 0.60f, 0.28f, SQ_COURSE }, { 0.86f, 0.17f, SQ_TOP } } },             /* SAIL_SQUARE2 */
    { 3, { { 0.55f, 0.28f, SQ_COURSE }, { 0.80f, 0.17f, SQ_TOP }, { 0.96f, 0.10f, SQ_TOP } } },   /* SAIL_SQUARE3 */
    { 0, { { 0 } } },                                                             /* SAIL_LATEEN (separate path) */
};

// Cordage is a few pixels wide: below this the ropes are noise and the sails carry the
// silhouette on their own, so the whole rig is skipped.
#define RIG_MIN_PX 90.f

// one rope or spar as a cross of two double-sided quads: a single quad vanishes edge-on, and a
// tube would cost twenty times as much for something a few pixels wide
static void rig_bar(mb_t *m, const float a[3], const float b[3], float w, const float *col) {
    float d[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float dl = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (dl < 1e-5f) return;
    d[0] /= dl; d[1] /= dl; d[2] /= dl;
    float up[3] = { 0, 1, 0 };
    if (fabsf(d[1]) > 0.95f) { up[0] = 1; up[1] = 0; }
    float u[3] = { d[1]*up[2] - d[2]*up[1], d[2]*up[0] - d[0]*up[2], d[0]*up[1] - d[1]*up[0] };
    float ul = sqrtf(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]); if (ul < 1e-5f) return;
    u[0] = u[0]/ul*w; u[1] = u[1]/ul*w; u[2] = u[2]/ul*w;
    float v[3] = { d[1]*u[2] - d[2]*u[1], d[2]*u[0] - d[0]*u[2], d[0]*u[1] - d[1]*u[0] };
    float vl = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); if (vl > 1e-5f) { v[0]=v[0]/vl*w; v[1]=v[1]/vl*w; v[2]=v[2]/vl*w; }
    for (int k = 0; k < 2; ++k) {
        const float *o = k ? v : u;
        float p0[3] = { a[0]-o[0], a[1]-o[1], a[2]-o[2] }, p1[3] = { b[0]-o[0], b[1]-o[1], b[2]-o[2] };
        float p2[3] = { b[0]+o[0], b[1]+o[1], b[2]+o[2] }, p3[3] = { a[0]+o[0], a[1]+o[1], a[2]+o[2] };
        mb_quad2(m, p0, p1, p2, p3, col[0], col[1], col[2], 0.f);
    }
}
static void rig_rope(mb_t *m, const float a[3], const float b[3], float w) { rig_bar(m, a, b, w, rope_col); }

// How free a strip boundary is, as the 32-level code: t = 0 at the spar, 1 at the loose edge.
static int cloth_level(float t) { int l = (int)(t * 31.f + 0.5f); return l < 0 ? 0 : l > 31 ? 31 : l; }

// A square sail bent to a yard at `top`, hanging to `bot`, half-width `hw`, its plane at x.
// Built as a grid so the belly can be shaped between the leeches as well as from head to foot.
// spars == NULL: an authored yard stands there (the square_yard asset, or the bowsprit's own
// hanging yard), so no procedural bar is drawn - the canvas alone.
static void rig_sail_square(mb_t *cloth, mb_t *spars, float x, float top, float bot, float hw) {
    const int N = 6, M = 4;
    float ya[3] = { x, top, -hw * 1.06f }, yb[3] = { x, top, hw * 1.06f };
    if (spars) rig_bar(spars, ya, yb, RIG_YARD_W, spar_col);
    for (int i = 0; i < N; ++i) {
        float t0 = i / (float)N, t1 = (i + 1) / (float)N;
        float y0 = top + (bot - top) * t0, y1 = top + (bot - top) * t1;
        float w0 = hw * (0.94f + 0.06f * t0), w1 = hw * (0.94f + 0.06f * t1);   // a little wider at the foot
        int l0 = cloth_level(t0), l1 = cloth_level(t1);
        for (int j = 0; j < M; ++j) {
            float u0 = j / (float)M * 2.f - 1.f, u1 = (j + 1) / (float)M * 2.f - 1.f;
            float p0[3] = { x, y0, w0 * u0 }, p1[3] = { x, y0, w0 * u1 };
            float p2[3] = { x, y1, w1 * u1 }, p3[3] = { x, y1, w1 * u0 };
            mb_quad_cloth(cloth, p0, p1, p2, p3, nrm_x,
                          mb_cloth_code(l0, u0), mb_cloth_code(l0, u1), mb_cloth_code(l1, u1), mb_cloth_code(l1, u0),
                          hw, sail_col[0], sail_col[1], sail_col[2], 0.f);
        }
    }
}

// A lateen: a yard raked from low forward (the tack) to high aft (the peak, above the
// masthead), the canvas hanging from it to the clew near the deck aft - cut with a short luff
// at the tack, as the settee rig the galleys actually carried. `h` is the mast height; the
// sail is sized by it. Bellies sideways (its normal is +z), sheeted to one side of the mast.
static void rig_sail_lateen(mb_t *cloth, mb_t *spars, float sx, float dy, float h) {
    // spars == NULL: the caller stands an authored lantina there (shipyard_push), canvas only
    const int N = 5, M = 6;
    float z = 0.012f;
    float T[3] = { sx + 0.42f * h, dy + 0.22f * h, z }, P[3] = { sx - 0.30f * h, dy + 1.02f * h, z };   // the yard
    float F[3] = { sx + 0.40f * h, dy + 0.12f * h, z }, C[3] = { sx - 0.34f * h, dy + 0.14f * h, z };   // the foot
    float ya[3] = { T[0] + 0.05f * h, T[1] - 0.06f * h, 0 }, yb[3] = { P[0] - 0.04f * h, P[1] + 0.05f * h, 0 };
    if (spars) rig_bar(spars, ya, yb, RIG_YARD_W, spar_col);
    float hw = 0.36f * h;
    for (int i = 0; i < N; ++i) {
        float w0 = i / (float)N, w1 = (i + 1) / (float)N;
        int l0 = cloth_level(w0), l1 = cloth_level(w1);
        for (int j = 0; j < M; ++j) {
            float t0 = j / (float)M, t1 = (j + 1) / (float)M;
            float u0 = t0 * 2.f - 1.f, u1 = t1 * 2.f - 1.f;
            float q[4][3];
            const float tt[4] = { t0, t1, t1, t0 }, ww[4] = { w0, w0, w1, w1 };
            for (int k = 0; k < 4; ++k) for (int a = 0; a < 3; ++a) {
                float yard = T[a] + (P[a] - T[a]) * tt[k], foot = F[a] + (C[a] - F[a]) * tt[k];
                q[k][a] = yard + (foot - yard) * ww[k];
            }
            mb_quad_cloth(cloth, q[0], q[1], q[2], q[3], nrm_z,
                          mb_cloth_code(l0, u0), mb_cloth_code(l0, u1), mb_cloth_code(l1, u1), mb_cloth_code(l1, u0),
                          hw, sail_col[0], sail_col[1], sail_col[2], 0.f);
        }
    }
}

// Build both meshes for a class. `hb` is the hull's half beam and `ty[]` the mast tops, all in
// fractions of hull length measured from the waterline.
static void rig_build(rig_cache_t *rc, const ship_class_t *c, int nm, const float *sx,
                      const float *ty, float dy, float hb, const float *flagcol) {
    mb_t rope = {0}, cloth = {0};
    mb_material(&rope, 0.85f, 0.f);
    mb_material(&cloth, 0.92f, 0.f);
    float bow = 0.47f, stern = -0.47f;
    int main = 0;
    for (int k = 1; k < nm; ++k) if (c->mast_h[k] > c->mast_h[main]) main = k;
    for (int k = 0; k < nm; ++k) {
        float top[3] = { sx[k], ty[k], 0 };
        // shrouds: three a side from the masthead to the channels, raked a little aft
        for (int i = 0; i < 3; ++i) {
            float f = (i - 1) * 0.055f;
            for (int side = -1; side <= 1; side += 2) {
                float foot[3] = { sx[k] + f - 0.02f, dy, side * hb * (0.86f - 0.06f * i) };
                rig_rope(&rope, top, foot, RIG_ROPE_W);
            }
        }
        // forestay to the next mast's foot (or the bow), backstay to the stern
        float fwd[3] = { k + 1 < nm && sx[k + 1] > sx[k] ? sx[k + 1] : bow,
                         k + 1 < nm && sx[k + 1] > sx[k] ? dy + (ty[k + 1] - dy) * 0.18f : dy + 0.012f, 0 };
        rig_rope(&rope, top, fwd, RIG_ROPE_W);
        float aft[3] = { k > 0 ? sx[k - 1] : stern, dy + 0.01f, 0 };
        rig_rope(&rope, top, aft, RIG_ROPE_W);
        // ratlines on the main shrouds, so the rig reads as climbable up close
        if (k == main) {
            for (int r = 1; r <= 5; ++r) {
                float t = r / 6.f, y = dy + (ty[k] - dy) * t * 0.72f;
                float w = hb * 0.86f * (1.f - t * 0.72f);
                float ax = sx[k] - 0.02f - 0.055f * (1.f - t);
                for (int side = -1; side <= 1; side += 2) {
                    float p0[3] = { ax, y, side * w * 0.55f }, p1[3] = { ax, y, side * w };
                    rig_rope(&rope, p0, p1, RIG_ROPE_W * 0.7f);
                }
            }
        }
        // Canvas is not built here any more: every sail - square and lateen - is a per-ship
        // instance on its yard's braced heading (shipyard_push), hanging from the authored
        // square_yard / lateen_yard assets. Only the standing rigging stays in this mesh.
    }
    if (c->spritsail) {   // the bowsprit, steeved up from the forecastle, with its spritsail slung under it
        if (!asset_find("bowsprit")) {   // authored spar in the registry: shipyard_push stands it instead
            float a[3] = { bow - 0.04f, dy + 0.05f, 0 }, b[3] = { bow + 0.20f, dy + 0.16f, 0 };
            rig_bar(&rope, a, b, RIG_YARD_W, spar_col);
        }
        rig_sail_square(&cloth, asset_find("bowsprit") ? NULL : &rope, bow + 0.10f, dy + 0.13f, dy + 0.03f, 0.08f);
    }
    {   // the nation's flag at the main masthead: `mat` 1 takes the instance colour. Built
        // trailing aft; the shader swings it into the apparent wind.
        float y = ty[main] + 0.012f, x = sx[main] - 0.004f, fl = RIG_FLAG_LEN, fh = RIG_FLAG_H;
        const int N = 4;
        for (int i = 0; i < N; ++i) {
            float t0 = i / (float)N, t1 = (i + 1) / (float)N;
            int l0 = cloth_level(t0), l1 = cloth_level(t1);
            float x0 = x - fl * (l0 / 31.f), x1 = x - fl * (l1 / 31.f);   // placed by the quantised level, as the shader will read it
            float p0[3] = { x0, y, 0 }, p1[3] = { x0, y + fh, 0 }, p2[3] = { x1, y + fh, 0 }, p3[3] = { x1, y, 0 };
            mb_quad_cloth(&cloth, p0, p1, p2, p3, nrm_z,
                          mb_cloth_code(l0, -1.f), mb_cloth_code(l0, 1.f), mb_cloth_code(l1, 1.f), mb_cloth_code(l1, -1.f),
                          fl, flagcol[0], flagcol[1], flagcol[2], 1.f);
        }
    }
    rc->rope  = mesh_upload(&rope);
    rc->cloth = mesh_upload(&cloth);
    rc->built = 1;
}

static const ship_class_t *by_name(const char *n) {
    for (int i = 0; i < NCLASSES; ++i) if (!strcmp(classes[i].name, n)) return &classes[i];
    return NULL;
}

// Venice, the Ottomans and the pirates sail the oared interceptors the sheets call
// Mediterranean; the northern trading powers favour the broad-beamed hulk over the carrack.
static int med_flavour(int nation)   { return nation == NATION_VENICE || nation == NATION_OTTOMAN || nation == NATION_PIRATES; }
static int north_flavour(int nation) { return nation == NATION_ENGLAND || nation == NATION_NETHERLANDS || nation == NATION_DENMARK; }

// A game ship type is a role, not a hull: the same Frigate is an armed cog in the Channel and a
// galiot in the Adriatic.
static const ship_class_t *class_for(int type, int nation) {
    switch (type) {
    case SHIP_SLOOP:    return med_flavour(nation) ? by_name("fusta") : by_name("coastal_trader");
    case SHIP_FLUYT:    return med_flavour(nation) ? by_name("galiot") : by_name("cog");
    case SHIP_MERCHANT: return north_flavour(nation) ? by_name("hulk") : by_name("carrack");
    case SHIP_FRIGATE:  return med_flavour(nation) ? by_name("galiot") : by_name("armed_cog");
    case SHIP_GALLEON:  return by_name("war_carrack");
    case SHIP_LINER:    return by_name("great_carrack");
    default:            return NULL;
    }
}

const char *shipyard_class_name(int type, int nation) {
    const ship_class_t *c = class_for(type, nation);
    return c ? c->name : "procedural";
}

int shipyard_class_stats(int type, int nation, ship_stats_t *out) {
    const ship_class_t *c = class_for(type, nation);
    if (c) { *out = c->st; return 1; }
    // no authored class: numbers from the gameplay row, so callers never divide by zero
    const ship_info_t *t = &ship_types[type < 0 ? 0 : type >= SHIP_COUNT ? SHIP_COUNT - 1 : type];
    float len = ship_scale(type) / SHIP_M_PER_UNIT;
    *out = (ship_stats_t){ len, len * 0.28f, len * 0.10f, len * len * 0.35f, t->speed * 5.f, 0.f, 120.f / len, t->guns, t->crew };
    return 0;
}

// The mast standing at station k: the per-type library part first (mast_lib, the authored
// assemblies from assets/ships/new_masts), else the class's old split part (`_m<part>`), else
// the single narrowed `_m0`, else the whole generated rig - a ship always gets *some* mast.
static asset_t *mast_asset(const ship_class_t *c, int k) {
    if (c->mast_lib[k] && asset_find(c->mast_lib[k]))
        return asset_get_role(ASSET_SHIP_PART, c->mast_lib[k]);
    char role[80];
    if (c->split) { snprintf(role, sizeof role, "%s_m%d", c->masts, c->mast_part[k]); return asset_get_role(ASSET_SHIP_PART, role); }
    snprintf(role, sizeof role, "%s_m0", c->masts);
    asset_t *a = asset_find(role) ? asset_get_role(ASSET_SHIP_PART, role) : NULL;
    return a ? a : asset_get_role(ASSET_SHIP_PART, c->masts);
}

void shipyard_request_all(void) {
    for (int i = 0; i < NCLASSES; ++i) {
        const ship_class_t *c = &classes[i];
        asset_t *a; char role[80];
        if ((a = asset_find(c->hull)))             asset_request(a);
        for (int k = 0; k < SHIP_MAX_MASTS; ++k)
            if (c->mast_lib[k] && (a = asset_find(c->mast_lib[k]))) asset_request(a);
        if (c->masts) {
            if ((a = asset_find(c->masts))) asset_request(a);
            for (int k = 0; k < 3; ++k) { snprintf(role, sizeof role, "%s_m%d", c->masts, k); if ((a = asset_find(role))) asset_request(a); }
        }
        if (c->oars  && (a = asset_find(c->oars)))  asset_request(a);
    }
    // the shared spars of the new mast library: the swinging lantina, the braced square yard
    // and the bowsprit
    { asset_t *a;
      if ((a = asset_find("lateen_yard"))) asset_request(a);
      if ((a = asset_find("square_yard"))) asset_request(a);
      if ((a = asset_find("bowsprit")))    asset_request(a); }
}

int shipyard_push(const shipyard_req_t *r) {
    if (!shipyard_enabled) return 0;
    const ship_class_t *c = class_for(r->type, r->nation);
    if (!c) return 0;
    asset_t *hull = asset_get_role(ASSET_SHIP, c->hull);    // requests it on first use
    if (!hull) { shipyard_fallbacks++; return 0; }          // still loading, or never imported

    // The asset is normalised to its own length, so the instance scale only carries the
    // stand-in stretch and the scene's own magnification (a staged battle draws ships several
    // times larger than the map does). Everything below is in world units of the fitted hull.
    float cscale = c->model_scale * (r->scale > 0.f ? r->scale : 1.f);
    float len = c->length_m * SHIP_M_PER_UNIT * (r->scale > 0.f ? r->scale : 1.f);
    float hh  = (hull->bmax[1] - hull->bmin[1]) * cscale;   // fitted hull height, keel to rail
    // Buoyancy from the class's displacement. A fusta is thrown about by every sea; a laden
    // great carrack shoulders through it. `resp` scales how much of the swell reaches the hull
    // (the bob/ride factor the mesh shader multiplies the wave field by) and how far she rolls,
    // and the heavy classes float lower on their lines - gravity against buoyancy, per tonne.
    float resp  = clampf(1.45f - 0.22f * log10f(maxf(c->st.tonnes, 1.f)), 0.55f, 1.15f);
    float water = hh * c->draft * (1.f + 0.18f * (1.05f - resp));   // the pivot is the keel (base_center)
    float fx = cosf(r->heading), fz = -sinf(r->heading);    // forward (heading 0 = +x, as fleet_t)
    float sx = -fz, sz = fx;                                // starboard

    inst_t in = (inst_t){ r->x, -water, r->z, r->heading, cscale,
                          r->cr, r->cg, r->cb, r->rock * resp, r->sel, resp, 0.f, r->dither };
    scene_asset_instance(hull, in, r->key, r->camdist);
    shipyard_ships_drawn++;

    int nm = c->nmasts > 0 ? (c->nmasts > SHIP_MAX_MASTS ? SHIP_MAX_MASTS : c->nmasts) : 1;
    int main = 0;
    for (int k = 1; k < nm; ++k) if (c->mast_h[k] > c->mast_h[main]) main = k;
    #define MAST_OVERHANG 1.0f
    #define MAST_TRIM     0.85f
    asset_t *mref = NULL;                                   // the mainmast sizes the rig
    if (c->masts) {
        asset_t *mp[SHIP_MAX_MASTS] = { 0 };
        for (int k = 0; k < nm; ++k) mp[k] = mast_asset(c, k);
        mref = mp[main];
        if (mref) {                                         // masts stand on the deck, not the keel
            // A rig is sized by its height (the design sheets give mast heights), but the
            // generated assemblies do not share an aspect ratio: some carry a long bowsprit and
            // lateen yards and come out two thirds longer than their own hull. A whole rig may
            // not overhang its own hull, so anything longer is scaled down until it fits; the
            // trim then takes the generated spars - which are chunky for their height - back to
            // something that reads as rigging rather than tree trunks.
            float ml = (mref->bmax[0] - mref->bmin[0]) * cscale;
            float ms = cscale * MAST_TRIM;
            if (ml > len * MAST_OVERHANG) ms = cscale * MAST_TRIM * len * MAST_OVERHANG / ml;
            float deck_y = -water + hh * c->deck;
            for (int k = 0; k < nm; ++k) {
                if (!mp[k]) continue;
                float dx = c->mast_at[k] * len;
                inst_t mi = in;
                mi.x = r->x + fx * dx; mi.z = r->z + fz * dx;
                mi.y = deck_y;
                mi.ax = r->x; mi.az = r->z;   // a mast is a fixed part of its ship: same swell, same roll (i_anchor)
                // Every mast part is fitted to the class's rig height, so the fore/main/mizzen
                // proportion comes from the class table either way - which keeps it a number
                // you can tune without re-exporting anything.
                mi.scale = ms * (c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f);
                scene_asset_instance(mp[k], mi, r->key ^ (unsigned)(0x5BD1u + k * 0x97u), r->camdist);
            }
        }
    }

    // Rigging and canvas: one instance each of the class's procedural rope and cloth meshes,
    // built the first time both the hull and the rig are resident (their sizes set the mast
    // stations). Skipped once the ship is small enough that cordage is below a pixel.
    if (lod_px(len * 0.5f, r->camdist) > RIG_MIN_PX) {
        rig_cache_t *rc = &rig_cache[(int)(c - classes)];
        if (!rc->built && mref) {
            float ml = (mref->bmax[0] - mref->bmin[0]) * cscale, ms = MAST_TRIM;
            if (ml > len * MAST_OVERHANG) ms = MAST_TRIM * len * MAST_OVERHANG / ml;
            float mh = (mref->bmax[1] - mref->bmin[1]) * cscale * ms;   // fitted rig height, world units
            float sxn[SHIP_MAX_MASTS], tyn[SHIP_MAX_MASTS];
            float dy = (hh * c->deck - water) / len;              // deck above the waterline
            for (int k = 0; k < nm; ++k) {
                sxn[k] = c->mast_at[k];
                tyn[k] = dy + mh * (c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f) / len;
            }
            float hb = (hull->bmax[2] - hull->bmin[2]) * cscale * 0.5f / len;
            float fc[3] = { r->cr, r->cg, r->cb };
            rig_build(rc, c, nm, sxn, tyn, dy, hb, fc);
            LOG_DEBUG("gfx", "rigging built for '%s': %d masts, deck %.3f, rig %.3f of hull length",
                      c->name, nm, dy, mh / len);
        }
        if (rc->built) {
            float wdx, wdz, wstr; wind_at(r->x, r->z, G.day, &wdx, &wdz, &wstr);
            inst_t ri = in;
            ri.y = 0.f;                       // the rig mesh is modelled from the waterline
            ri.scale = len;                   // and in fractions of hull length
            *instlist_push(&rig_inst[(int)(c - classes)]) = ri;
            inst_t ci = ri; ci.fx1 = 1.f;     // cloth: the wind model in meshes.c
            ci.fx2 = wdx * wstr; ci.fx3 = wdz * wstr;
            *instlist_push(&cloth_inst[(int)(c - classes)]) = ci;
            shipyard_rigged++;

            // --- the lateen stations. The lantina (assets/ships/new_masts explanation.txt) is
            // its own authored spar, slung at ~0.62 of the mast and pivoting about the mast
            // axis; the canvas is the shared unit mesh instanced on the same pivot, so sail
            // and yard swing as one piece. The swing itself is the same stateless physics the
            // roll/pitch model uses: trimmed off the apparent wind, working a little harder
            // the harder it blows, each ship on its own phase.
            float deck_y2 = -water + hh * c->deck;
            float msl = cscale * MAST_TRIM;
            if (mref) { float ml2 = (mref->bmax[0] - mref->bmin[0]) * cscale;
                        if (ml2 > len * MAST_OVERHANG) msl = cscale * MAST_TRIM * len * MAST_OVERHANG / ml2; }
            float wang = atan2f(-wdz, wdx);
            float rel = wrap_angle(wang - r->heading);
            float swing = clampf(rel * 0.35f, -0.55f, 0.55f)
                        + (0.03f + 0.05f * wstr) * sinf(RF.time * (0.8f + 0.3f * wstr) + (float)(r->key & 31u) * 0.21f);
            asset_t *lantina = asset_find("lateen_yard") ? asset_get_role(ASSET_SHIP_PART, "lateen_yard") : NULL;
            asset_t *sqyard  = asset_find("square_yard") ? asset_get_role(ASSET_SHIP_PART, "square_yard") : NULL;
            // square yards brace less than a lantina trims: toward the wind, clamped, with the
            // same working oscillation on the ship's own phase
            float brace = clampf(rel * 0.30f, -0.50f, 0.50f)
                        + (0.02f + 0.04f * wstr) * sinf(RF.time * (0.7f + 0.3f * wstr) + (float)(r->key & 31u) * 0.17f);
            if (!sq_cloth_built) {
                for (int q = 0; q < SQ_KINDS; ++q) {
                    mb_t cl = (mb_t){0}; mb_material(&cl, 0.92f, 0.f);
                    rig_sail_square(&cl, NULL, 0.f, 0.f, -sq_drop[q], 1.f);
                    sq_cloth_mesh[q] = mesh_upload(&cl);
                }
                sq_cloth_built = 1;
            }
            for (int k = 0; k < nm; ++k) {
                asset_t *mk = mast_asset(c, k);
                if (!mk) continue;
                float mh = (mk->bmax[1] - mk->bmin[1]) * msl * (c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f);
                float dxk = c->mast_at[k] * len;
                float bx = r->x + fx * dxk, bz = r->z + fz * dxk;   // the mast axis
                if (c->sail[k] == SAIL_LATEEN) {
                    float sw = r->heading + swing;
                    if (!lateen_cloth_built) {
                        mb_t cl = (mb_t){0}; mb_material(&cl, 0.92f, 0.f);
                        rig_sail_lateen(&cl, NULL, 0.f, 0.f, 1.f);
                        lateen_cloth_mesh = mesh_upload(&cl); lateen_cloth_built = 1;
                    }
                    inst_t li = in;
                    li.heading = sw; li.x = bx; li.z = bz; li.y = deck_y2; li.scale = mh;
                    li.fx1 = 1.f; li.fx2 = wdx * wstr; li.fx3 = wdz * wstr;
                    li.ax = r->x; li.az = r->z;
                    *instlist_push(&lateen_cloth_inst) = li;
                    if (lantina) {
                        float yh = lantina->bmax[1] - lantina->bmin[1];
                        inst_t yi = in;
                        yi.heading = sw;
                        yi.scale = 0.90f * mh / maxf(yh, 0.01f);
                        yi.x = bx + cosf(sw) * 0.06f * mh; yi.z = bz - sinf(sw) * 0.06f * mh;
                        yi.y = deck_y2 + 0.62f * mh;      // the slung point (the model pivots there)
                        yi.ax = r->x; yi.az = r->z;
                        scene_asset_instance(lantina, yi, r->key ^ (unsigned)(0x77A1u + k * 0x33u), r->camdist);
                    }
                    continue;
                }
                // square sails: each layer's canvas and its authored yard on the braced heading
                const sq_layout_t *ly = &sq_layout[c->sail[k] >= 0 && c->sail[k] < 5 ? c->sail[k] : 0];
                if (!ly->n) continue;
                float bhd = r->heading + brace;
                float cb = cosf(bhd), sb = -sinf(bhd);
                float off = RIG_SAIL_OFF * len;           // canvas just fore of its mast, as before
                for (int l = 0; l < ly->n; ++l) {
                    float top_w = deck_y2 + mh * ly->l[l].top;
                    float hw_w  = mh * ly->l[l].hw;
                    inst_t qi = in;
                    qi.heading = bhd;
                    qi.x = bx + cb * off; qi.z = bz + sb * off;
                    qi.y = top_w; qi.scale = hw_w;
                    qi.fx1 = 1.f; qi.fx2 = wdx * wstr; qi.fx3 = wdz * wstr;
                    qi.ax = r->x; qi.az = r->z;
                    *instlist_push(&sq_cloth_inst[(int)ly->l[l].kind]) = qi;
                    if (sqyard) {
                        float yl = maxf(sqyard->bmax[0] - sqyard->bmin[0], sqyard->bmax[2] - sqyard->bmin[2]);
                        inst_t yi = in;
                        yi.heading = bhd;
                        yi.scale = 2.12f * hw_w / maxf(yl, 0.01f);   // the canvas hangs to 1.06 hw each side
                        yi.x = bx + cb * off; yi.z = bz + sb * off; yi.y = top_w;
                        yi.ax = r->x; yi.az = r->z;
                        scene_asset_instance(sqyard, yi, r->key ^ (unsigned)(0x51C3u + (k * 4 + l) * 0x2Bu), r->camdist);
                    }
                }
            }
            // --- the authored bowsprit, where the class carries one: stands where the
            // procedural spar stood (rig_build draws only the spritsail canvas then)
            if (c->spritsail && asset_find("bowsprit")) {
                asset_t *bs = asset_get_role(ASSET_SHIP_PART, "bowsprit");
                if (bs) {
                    float bl = bs->bmax[0] - bs->bmin[0];
                    inst_t bi = in;
                    bi.scale = 0.26f * len / maxf(bl, 0.01f);
                    float bxc = 0.55f * len;          // spar centre, just past the stem
                    bi.x = r->x + fx * bxc; bi.z = r->z + fz * bxc;
                    bi.y = deck_y2 + 0.10f * len;
                    bi.ax = r->x; bi.az = r->z;
                    scene_asset_instance(bs, bi, r->key ^ 0x3B5Au, r->camdist);
                }
            }
        }
    }

    // Oar bank: one instance per oar, both sides, swept a little aft and dipped outboard. Only
    // while the ship is large on screen - an oar is a twentieth of the hull.
    if (c->oars && c->oar_pairs > 0 && lod_px(len * 0.5f, r->camdist) > OAR_MIN_PX) {
        asset_t *oar = asset_get_role(ASSET_SHIP_PART, c->oars);
        if (oar) {
            float span = c->oar_span * len, half = c->beam_frac * len;
            float olen = (oar->bmax[0] - oar->bmin[0]) * cscale;   // the oar is modelled along +x
            float out = half * 0.96f + olen * OAR_INBOARD;         // centre of the oar, just outboard of the rail
            float y = -water + hh * c->deck * 0.72f;        // the oar ports sit below the rail
            float pull = clampf(r->rock, 0.f, 1.6f);                      // how hard she is working
            // A sweep is pulled at 20-26 strokes a minute, not the fast churn this used to run:
            // rate ~2.2 rad/s flat out. The blade dips on the pull and lifts on the recovery -
            // the instance transform has only yaw and height, and height is exactly the part of
            // the cycle that reads from the quarterdeck.
            float rate = 0.9f + 1.3f * pull, reach = 0.07f + 0.20f * pull;
            float oph = RF.time * rate + (float)(r->key & 63u) * 0.09f;
            float stroke = reach * sinf(oph);
            float dip = 0.012f * len * clampf(cosf(oph), 0.f, 1.f) * (0.3f + 0.7f * pull);
            const float side_sign = 1.f;
            for (int i = 0; i < c->oar_pairs; ++i) {
                float t = c->oar_pairs > 1 ? i / (float)(c->oar_pairs - 1) - 0.5f : 0.f;
                float along = t * span;
                for (int side = -1; side <= 1; side += 2) {
                    inst_t oi = in;
                    oi.x = r->x + fx * along + sx * out * side;
                    oi.z = r->z + fz * along + sz * out * side;
                    oi.y = y - dip;
                    // Blade outboard, loom inboard: the oar model points its blade along -x
                    // once the ship assets are turned bow-to-+x, so the sweep is the mirror of
                    // the obvious one - the obvious one buries the blades in the hull.
                    //
                    // The stroke: the whole bank swings together, as a rowed ship does, at a
                    // rate and reach that follow how hard she is being pulled. A ship lying
                    // still keeps her oars out and level. Only a yaw sweep - the instance
                    // transform has no other axis - which is the part of a stroke that reads.
                    oi.ax = r->x; oi.az = r->z;   // the bank rides the hull's swell, not its own
                    oi.heading = r->heading - side * (1.5708f + stroke * side_sign);
                    oi.r = 0.62f; oi.g = 0.52f; oi.b = 0.36f;   // oak, not the nation's colours
                    oi.sel = 0.f;
                    scene_asset_instance(oar, oi, r->key ^ (unsigned)(0x9E37u + i * 2 + (side > 0)), r->camdist);
                    shipyard_oars_drawn++;
                }
            }
        }
    }
    return 1;
}


// Per frame: the instance lists are rebuilt from scratch (scene.c and detailview.c both call
// this before pushing ships), then uploaded and drawn with whatever pipeline is bound.
void shipyard_frame_begin(void) {
    for (int i = 0; i < NCLASSES; ++i) { instlist_clear(&rig_inst[i]); instlist_clear(&cloth_inst[i]); }
    instlist_clear(&lateen_cloth_inst);
    for (int q = 0; q < SQ_KINDS; ++q) instlist_clear(&sq_cloth_inst[q]);
    shipyard_rigged = 0;
}

void shipyard_upload(void) {
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_set_instances(&rig_cache[i].rope,  rig_inst[i].v,   rig_inst[i].n);
        mesh_set_instances(&rig_cache[i].cloth, cloth_inst[i].v, cloth_inst[i].n);
    }
    if (lateen_cloth_built) mesh_set_instances(&lateen_cloth_mesh, lateen_cloth_inst.v, lateen_cloth_inst.n);
    if (sq_cloth_built) for (int q = 0; q < SQ_KINDS; ++q) mesh_set_instances(&sq_cloth_mesh[q], sq_cloth_inst[q].v, sq_cloth_inst[q].n);
}

int shipyard_submit(void) {
    int n = 0;
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_draw(&rig_cache[i].rope);  n += rig_cache[i].rope.ninst;
        mesh_draw(&rig_cache[i].cloth); n += rig_cache[i].cloth.ninst;
    }
    if (lateen_cloth_built) { mesh_draw(&lateen_cloth_mesh); n += lateen_cloth_mesh.ninst; }
    if (sq_cloth_built) for (int q = 0; q < SQ_KINDS; ++q) { mesh_draw(&sq_cloth_mesh[q]); n += sq_cloth_mesh[q].ninst; }
    return n;
}
