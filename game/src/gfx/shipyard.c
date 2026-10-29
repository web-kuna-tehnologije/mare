// Authored ship assembly (see shipyard.h, game/docs/DESIGN_SHIPS.md).
// Sections: the Gen I class table · game ship type -> class · geometry (waterline, deck, oar
// bank) · submission through the authored-asset path.
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
#define OAR_INBOARD 0.35f

#define SHIP_MAX_MASTS 3

typedef struct ship_class_t {
    const char *name;
    const char *hull, *masts, *oars;    // asset roles ("" / NULL when the class has none)
    float length_m;                     // design sheet length overall (midpoint of the range)
    float draft;                        // waterline above the keel, fraction of hull height
    float deck;                         // deck above the keel, fraction of hull height
    // How many masts to step along the hull, and where each comes from. Meshy generates a rig
    // as one object; tools/blender_split_masts.py cuts the three-mast assemblies into
    // `<masts>_m0` (foremast) .. `_m2` (mizzen), narrowed and each standing on its own origin,
    // so every mast can be placed at the station its design sheet gives. Classes whose model
    // was already a single mast (`cog_masts`, `armed_cog_masts`, `mediterranean_masts`) keep
    // using `masts` itself and simply step it `nmasts` times.
    int   split;                        // 1 = the parts are <masts>_m<k>, 0 = step `masts` itself
    int   nmasts;
    float mast_at[SHIP_MAX_MASTS];      // station along the hull, fraction of length (+ = bow)
    float mast_h[SHIP_MAX_MASTS];       // height of each, relative to the fitted model
    int   lateen;                       // lateen rig: triangular sails on raked yards
    int   oar_pairs;                    // per side
    float oar_span;                     // bank length along the hull, fraction of length
    float beam_frac;                    // half-beam at the oar ports, fraction of length
} ship_class_t;

// The Gen I fleet as drawn in assets/ships/design. Hull and mast models come in pairs from
// Meshy except the galiot, which borrows the generic Mediterranean lateen rig.
static const ship_class_t classes[] = {
  // name             hull                   masts                   oars         len  draft deck sp  n  stations              heights             lat pairs span  beam
  { "coastal_trader", "coastal_trader_hull", "coastal_trader_masts", NULL,         21.f, .38f, .60f, 1, 3, { .26f,-.02f,-.28f },{ .80f, 1.f, .70f }, 0,  0, 0.f,  .16f },
  { "cog",            "cog_hull",            "cog_masts",            NULL,         25.f, .38f, .60f, 0, 1, { .02f },            { 1.f },             0,  0, 0.f,  .17f },   // a cog carries one mast
  { "armed_cog",      "armed_cog_hull",      "armed_cog_masts",      NULL,         32.f, .38f, .60f, 0, 3, { .24f,-.01f,-.27f },{ .78f, 1.f, .68f }, 0,  0, 0.f,  .16f },   // single-mast model, stepped three times
  { "hulk",           "hulk_hull",           "hulk_masts",           NULL,         35.f, .38f, .62f, 1, 3, { .25f,-.01f,-.27f },{ .82f, 1.f, .70f }, 0,  0, 0.f,  .19f },
  { "carrack",        "carrack_hull",        "carrack_masts",        NULL,         38.f, .36f, .58f, 1, 3, { .26f,-.01f,-.28f },{ .80f, 1.f, .68f }, 0,  0, 0.f,  .16f },
  { "war_carrack",    "war_carrack_hull",    "war_carrack_masts",    NULL,         47.f, .36f, .58f, 1, 3, { .27f,-.01f,-.29f },{ .80f, 1.f, .70f }, 0,  0, 0.f,  .16f },
  { "galiot",         "galiot_hull",         "mediterranean_masts",  "galley_oar", 28.f, .32f, .50f, 0, 3, { .26f,-.02f,-.28f },{ .80f, 1.f, .66f }, 1, 22, .62f, .11f },  // one lateen mast, stepped three times
  { "fusta",          "fusta_hull",          "fusta_masts",          "galley_oar", 25.f, .32f, .50f, 1, 3, { .24f,-.02f,-.26f },{ .85f, 1.f, .72f }, 1, 20, .60f, .09f },
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
// (`fx1` 0) and the canvas is cloth (`fx1` 1, which turns on the flutter in meshes.c). Within
// the cloth mesh, each horizontal strip carries its own "how free is this vertex" weight in the
// metalness channel - 0 at the yard, 1 at the foot - so a sail billows from its head downwards
// instead of sliding about as a rigid sheet.

#define RIG_ROPE_W   0.0035f        // rope half-width, fractions of hull length
#define RIG_SAIL_W   0.0015f        // tiny inset so canvas never z-fights its yard
static const float rope_col[3] = { 0.10f, 0.085f, 0.075f };   // tarred hemp, nearly black
static const float sail_col[3] = { 0.86f, 0.83f, 0.74f };     // unbleached canvas

typedef struct rig_cache_t { mesh_t rope, cloth; int built; unsigned gen; } rig_cache_t;
static rig_cache_t rig_cache[NCLASSES];
static instlist_t  rig_inst[NCLASSES], cloth_inst[NCLASSES];

// Cordage is a few pixels wide: below this the ropes are noise and the sails carry the
// silhouette on their own, so the whole rig is skipped.
#define RIG_MIN_PX 90.f

// one rope as a cross of two double-sided quads: a single quad vanishes edge-on, and a tube
// would cost twenty times as much for something a few pixels wide
static void rig_rope(mb_t *m, const float a[3], const float b[3], float w) {
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
        mb_quad2(m, p0, p1, p2, p3, rope_col[0], rope_col[1], rope_col[2], 0.f);
    }
}

// A square sail bent to a yard at `top`, hanging to `bot`, half-width `hw`, bellied forward by
// `belly`. Built as horizontal strips so the cloth weight can grow towards the foot.
static void rig_sail_square(mb_t *m, float x, float top, float bot, float hw, float belly) {
    const int N = 5;
    for (int i = 0; i < N; ++i) {
        float t0 = i / (float)N, t1 = (i + 1) / (float)N;
        float y0 = top + (bot - top) * t0, y1 = top + (bot - top) * t1;
        float b0 = belly * sinf(t0 * 3.14159f * 0.8f), b1 = belly * sinf(t1 * 3.14159f * 0.8f);
        float w0 = hw * (0.92f + 0.08f * t0), w1 = hw * (0.92f + 0.08f * t1);
        mb_material(m, 0.92f, 0.f);
        float p0[3] = { x + b0, y0, -w0 }, p1[3] = { x + b0, y0,  w0 };
        float p2[3] = { x + b1, y1,  w1 }, p3[3] = { x + b1, y1, -w1 };
        mb_quad_grad(m, p0, p1, p2, p3, t0, t1, sail_col[0], sail_col[1], sail_col[2], 0.f);
    }
}

// A lateen sail: a triangle on a raked yard running from low forward to high aft.
static void rig_sail_lateen(mb_t *m, float x, float top, float bot, float len_f, float hw) {
    const int N = 5;
    for (int i = 0; i < N; ++i) {
        float t0 = i / (float)N, t1 = (i + 1) / (float)N;
        float y0 = top + (bot - top) * t0, y1 = top + (bot - top) * t1;
        float x0 = x + len_f * t0, x1 = x + len_f * t1;          // the yard rakes forward as it drops
        float w0 = hw * t0, w1 = hw * t1;                         // widening towards the foot
        mb_material(m, 0.92f, 0.f);
        float p0[3] = { x0, y0, -w0 }, p1[3] = { x0, y0,  w0 };
        float p2[3] = { x1, y1,  w1 }, p3[3] = { x1, y1, -w1 };
        mb_quad_grad(m, p0, p1, p2, p3, t0, t1, sail_col[0], sail_col[1], sail_col[2], 0.f);
    }
}

// Build both meshes for a class. `hb` is the hull's half beam and `ty[]` the mast tops, all in
// fractions of hull length measured from the waterline.
static void rig_build(rig_cache_t *rc, const ship_class_t *c, int nm, const float *sx,
                      const float *ty, float dy, float hb, const float *flagcol) {
    mb_t rope = {0}, cloth = {0};
    mb_material(&rope, 0.85f, 0.f);
    float bow = 0.47f, stern = -0.47f;
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
        float fwd[3] = { k + 1 < nm ? sx[k + 1] : bow, k + 1 < nm ? dy + (ty[k + 1] - dy) * 0.18f : dy + 0.012f, 0 };
        rig_rope(&rope, top, fwd, RIG_ROPE_W);
        float aft[3] = { k > 0 ? sx[k - 1] : stern, dy + 0.01f, 0 };
        rig_rope(&rope, top, aft, RIG_ROPE_W);
        // ratlines on the aftermost shroud pair, so the rig reads as climbable up close
        if (k == (nm > 1 ? 1 : 0)) {
            for (int r = 1; r <= 5; ++r) {
                float t = r / 6.f, y = dy + (ty[k] - dy) * t * 0.72f;
                float w = hb * 0.86f * (1.f - t * 0.72f);
                float a[3] = { sx[k] - 0.02f - 0.055f * (1.f - t), y, -w }, b[3] = { a[0], y, w };
                (void)b;
                for (int side = -1; side <= 1; side += 2) {
                    float p0[3] = { a[0], y, side * w * 0.55f }, p1[3] = { a[0], y, side * w };
                    rig_rope(&rope, p0, p1, RIG_ROPE_W * 0.7f);
                }
            }
        }
        // canvas
        float h = ty[k] - dy;
        if (c->lateen) rig_sail_lateen(&cloth, sx[k] - 0.01f, dy + h * 0.92f, dy + h * 0.22f, 0.20f, hb * 1.05f);
        else           rig_sail_square(&cloth, sx[k] - 0.012f, dy + h * 0.80f, dy + h * 0.34f, hb * 0.95f, 0.035f);
    }
    {   // the nation's flag at the main masthead: `mat` 1 takes the instance colour
        int k = nm > 1 ? 1 : 0;
        float y = ty[k] + 0.012f, x = sx[k] - 0.004f, fl = 0.085f, fh = 0.042f;
        for (int i = 0; i < 4; ++i) {
            float t0 = i / 4.f, t1 = (i + 1) / 4.f;
            mb_material(&cloth, 0.92f, 0.f);
            float p0[3] = { x - fl * t0, y,      0 }, p1[3] = { x - fl * t0, y + fh, 0 };
            float p2[3] = { x - fl * t1, y + fh, 0 }, p3[3] = { x - fl * t1, y,      0 };
            mb_quad_grad(&cloth, p0, p1, p2, p3, t0, t1, flagcol[0], flagcol[1], flagcol[2], 1.f);
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
// galiot in the Adriatic. `scale` stretches a class that stands in for a size it does not have
// its own model for (the Ship of the Line wants the Great Carrack, which is not imported yet).
static const ship_class_t *class_for(int type, int nation, float *scale) {
    *scale = 1.f;
    switch (type) {
    case SHIP_SLOOP:    return med_flavour(nation) ? by_name("fusta") : by_name("coastal_trader");
    case SHIP_FLUYT:    return med_flavour(nation) ? by_name("galiot") : by_name("cog");
    case SHIP_MERCHANT: return north_flavour(nation) ? by_name("hulk") : by_name("carrack");
    case SHIP_FRIGATE:  return med_flavour(nation) ? by_name("galiot") : by_name("armed_cog");
    case SHIP_GALLEON:  *scale = 0.92f; return by_name("war_carrack");
    case SHIP_LINER:    *scale = 1.18f; return by_name("war_carrack");   // stands in for the Great Carrack
    default:            return NULL;
    }
}

const char *shipyard_class_name(int type, int nation) {
    float s; const ship_class_t *c = class_for(type, nation, &s);
    return c ? c->name : "procedural";
}

void shipyard_request_all(void) {
    for (int i = 0; i < NCLASSES; ++i) {
        const ship_class_t *c = &classes[i];
        asset_t *a;
        if ((a = asset_find(c->hull)))             asset_request(a);
        if (c->masts && (a = asset_find(c->masts))) asset_request(a);
        if (c->oars  && (a = asset_find(c->oars)))  asset_request(a);
    }
}

int shipyard_push(const shipyard_req_t *r) {
    if (!shipyard_enabled) return 0;
    float cscale;
    const ship_class_t *c = class_for(r->type, r->nation, &cscale);
    if (!c) return 0;
    asset_t *hull = asset_get_role(ASSET_SHIP, c->hull);    // requests it on first use
    if (!hull) { shipyard_fallbacks++; return 0; }          // still loading, or never imported

    // The asset is normalised to its own length, so the instance scale only carries the
    // stand-in stretch and the scene's own magnification (a staged battle draws ships several
    // times larger than the map does). Everything below is in world units of the fitted hull.
    cscale *= r->scale > 0.f ? r->scale : 1.f;
    float len = c->length_m * SHIP_M_PER_UNIT * cscale;
    float hh  = (hull->bmax[1] - hull->bmin[1]) * cscale;   // fitted hull height, keel to rail
    float water = hh * c->draft;                            // the pivot is the keel (base_center)
    float fx = cosf(r->heading), fz = -sinf(r->heading);    // forward (heading 0 = +x, as fleet_t)
    float sx = -fz, sz = fx;                                // starboard

    inst_t in = (inst_t){ r->x, -water, r->z, r->heading, cscale,
                          r->cr, r->cg, r->cb, r->rock, r->sel, 1.f, 0.f, r->dither };
    scene_asset_instance(hull, in, r->key, r->camdist);
    shipyard_ships_drawn++;

    if (c->masts) {
        // A split rig has one asset per mast, so each is looked up on its own; an unsplit one
        // steps the same model at every station.
        asset_t *mp[SHIP_MAX_MASTS] = { 0 };
        int nm = c->nmasts > 0 ? (c->nmasts > SHIP_MAX_MASTS ? SHIP_MAX_MASTS : c->nmasts) : 1;
        if (c->split) {
            char role[80];
            for (int k = 0; k < nm; ++k) { snprintf(role, sizeof role, "%s_m%d", c->masts, k);
                                           mp[k] = asset_get_role(ASSET_SHIP_PART, role); }
        } else {
            asset_t *one = asset_get_role(ASSET_SHIP_PART, c->masts);
            for (int k = 0; k < nm; ++k) mp[k] = one;
        }
        asset_t *m = mp[nm > 1 ? 1 : 0];                    // the mainmast sizes the rig
        if (m) {                                            // masts stand on the deck, not the keel
            // A rig is sized by its height (the design sheets give mast heights), but the
            // generated assemblies do not share an aspect ratio: some carry a long bowsprit and
            // lateen yards and come out two thirds longer than their own hull. A whole rig may
            // not overhang its own hull, so anything longer is scaled down until it fits; the
            // trim then takes the generated spars - which are chunky for their height - back to
            // something that reads as rigging rather than tree trunks.
            #define MAST_OVERHANG 1.0f
            #define MAST_TRIM     0.85f
            float ml = (m->bmax[0] - m->bmin[0]) * cscale;
            float ms = cscale * MAST_TRIM;
            if (ml > len * MAST_OVERHANG) ms = cscale * MAST_TRIM * len * MAST_OVERHANG / ml;
            float deck_y = -water + hh * c->deck;
            for (int k = 0; k < nm; ++k) {
                if (!mp[k]) continue;
                float dx = c->mast_at[k] * len;
                inst_t mi = in;
                mi.x = r->x + fx * dx; mi.z = r->z + fz * dx;
                mi.y = deck_y;
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
        asset_t *m = NULL;
        if (c->masts) { char role[80];
            if (c->split) { snprintf(role, sizeof role, "%s_m1", c->masts); m = asset_get_role(ASSET_SHIP_PART, role); }
            if (!m) m = asset_get_role(ASSET_SHIP_PART, c->masts); }
        if (!rc->built && m) {
            float ml = (m->bmax[0] - m->bmin[0]) * cscale, ms = MAST_TRIM;
            if (ml > len * MAST_OVERHANG) ms = MAST_TRIM * len * MAST_OVERHANG / ml;
            float mh = (m->bmax[1] - m->bmin[1]) * cscale * ms;   // fitted rig height, world units
            int n = c->nmasts > 0 ? (c->nmasts > SHIP_MAX_MASTS ? SHIP_MAX_MASTS : c->nmasts) : 1;
            float sxn[SHIP_MAX_MASTS], tyn[SHIP_MAX_MASTS];
            float dy = (hh * c->deck - water) / len;              // deck above the waterline
            for (int k = 0; k < n; ++k) {
                sxn[k] = c->mast_at[k];
                tyn[k] = dy + mh * (c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f) / len;
            }
            float hb = (hull->bmax[2] - hull->bmin[2]) * cscale * 0.5f / len;
            float fc[3] = { r->cr, r->cg, r->cb };
            rig_build(rc, c, n, sxn, tyn, dy, hb, fc);
            LOG_DEBUG("gfx", "rigging built for '%s': %d masts, deck %.3f, rig %.3f of hull length",
                      c->name, n, dy, mh / len);
        }
        if (rc->built) {
            inst_t ri = in;
            ri.y = 0.f;                       // the rig mesh is modelled from the waterline
            ri.scale = len;                   // and in fractions of hull length
            *instlist_push(&rig_inst[(int)(c - classes)]) = ri;
            inst_t ci = ri; ci.fx1 = 1.f;     // cloth: turns on the flutter in meshes.c
            *instlist_push(&cloth_inst[(int)(c - classes)]) = ci;
            shipyard_rigged++;
        }
    }

    // Oar bank: one instance per oar, both sides, swept a little aft and dipped outboard. Only
    // while the ship is large on screen - an oar is a twentieth of the hull.
    if (c->oars && c->oar_pairs > 0 && lod_px(len * 0.5f, r->camdist) > OAR_MIN_PX) {
        asset_t *oar = asset_get_role(ASSET_SHIP_PART, c->oars);
        if (oar) {
            float span = c->oar_span * len, half = c->beam_frac * len;
            float olen = (oar->bmax[0] - oar->bmin[0]) * cscale;   // the oar is modelled along +x
            float out = half + olen * OAR_INBOARD;                 // centre of the oar, outboard of the rail
            float y = -water + hh * c->deck * 0.72f;        // the oar ports sit below the rail
            float pull = clampf(r->rock, 0.f, 1.6f);                      // how hard she is working
            float rate = 1.6f + 2.4f * pull, reach = 0.09f + 0.26f * pull;
            float stroke = reach * sinf(RF.time * rate + (float)(r->key & 63u) * 0.09f);
            const float side_sign = 1.f;
            for (int i = 0; i < c->oar_pairs; ++i) {
                float t = c->oar_pairs > 1 ? i / (float)(c->oar_pairs - 1) - 0.5f : 0.f;
                float along = t * span;
                for (int side = -1; side <= 1; side += 2) {
                    inst_t oi = in;
                    oi.x = r->x + fx * along + sx * out * side;
                    oi.z = r->z + fz * along + sz * out * side;
                    oi.y = y;
                    // Blade outboard, loom inboard: the oar model points its blade along -x
                    // once the ship assets are turned bow-to-+x, so the sweep is the mirror of
                    // the obvious one - the obvious one buries the blades in the hull.
                    //
                    // The stroke: the whole bank swings together, as a rowed ship does, at a
                    // rate and reach that follow how hard she is being pulled. A ship lying
                    // still keeps her oars out and level. Only a yaw sweep - the instance
                    // transform has no other axis - which is the part of a stroke that reads.
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
    shipyard_rigged = 0;
}

void shipyard_upload(void) {
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_set_instances(&rig_cache[i].rope,  rig_inst[i].v,   rig_inst[i].n);
        mesh_set_instances(&rig_cache[i].cloth, cloth_inst[i].v, cloth_inst[i].n);
    }
}

int shipyard_submit(void) {
    int n = 0;
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_draw(&rig_cache[i].rope);  n += rig_cache[i].rope.ninst;
        mesh_draw(&rig_cache[i].cloth); n += rig_cache[i].cloth.ninst;
    }
    return n;
}
