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
int shipyard_oars_drawn, shipyard_ships_drawn, shipyard_fallbacks;

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

typedef struct ship_class_t {
    const char *name;
    const char *hull, *masts, *oars;    // asset roles ("" / NULL when the class has none)
    float length_m;                     // design sheet length overall (midpoint of the range)
    float draft;                        // waterline above the keel, fraction of hull height
    float deck;                         // deck above the keel, fraction of hull height
    float mast_dx;                      // mast assembly along the hull, fraction of length (+ = bow)
    int   oar_pairs;                    // per side
    float oar_span;                     // bank length along the hull, fraction of length
    float beam_frac;                    // half-beam at the oar ports, fraction of length
} ship_class_t;

// The Gen I fleet as drawn in assets/ships/design. Hull and mast models come in pairs from
// Meshy except the galiot, which borrows the generic Mediterranean lateen rig.
static const ship_class_t classes[] = {
  // name             hull                   masts                   oars         len  draft deck  mdx  pairs span  beam
  { "coastal_trader", "coastal_trader_hull", "coastal_trader_masts", NULL,         21.f, .38f, .60f, .02f,  0, 0.f,  .16f },
  { "cog",            "cog_hull",            "cog_masts",            NULL,         25.f, .38f, .60f, .00f,  0, 0.f,  .17f },
  { "armed_cog",      "armed_cog_hull",      "armed_cog_masts",      NULL,         32.f, .38f, .60f, .00f,  0, 0.f,  .16f },
  { "hulk",           "hulk_hull",           "hulk_masts",           NULL,         35.f, .38f, .62f, .00f,  0, 0.f,  .19f },
  { "carrack",        "carrack_hull",        "carrack_masts",        NULL,         38.f, .36f, .58f, .00f,  0, 0.f,  .16f },
  { "war_carrack",    "war_carrack_hull",    "war_carrack_masts",    NULL,         47.f, .36f, .58f, .00f,  0, 0.f,  .16f },
  { "galiot",         "galiot_hull",         "mediterranean_masts",  "galley_oar", 28.f, .32f, .50f, .00f, 22, .62f, .11f },
  { "fusta",          "fusta_hull",          "fusta_masts",          "galley_oar", 25.f, .32f, .50f, .00f, 20, .60f, .09f },
};
#define NCLASSES ((int)(sizeof classes / sizeof classes[0]))

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
    // stand-in stretch. Everything below is in world units of the *fitted* hull.
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
        asset_t *m = asset_get_role(ASSET_SHIP_PART, c->masts);
        if (m) {                                            // masts stand on the deck, not the keel
            // A rig is sized by its height (the design sheets give mast heights), but the
            // generated assemblies do not share an aspect ratio: some carry a long bowsprit and
            // lateen yards and come out two thirds longer than their own hull. A bowsprit may
            // overhang - a rig floating off the bow may not - so anything past MAST_OVERHANG of
            // the hull length is scaled down uniformly until it fits.
            #define MAST_OVERHANG 1.15f
            float ml = (m->bmax[0] - m->bmin[0]) * cscale;
            float ms = cscale;
            if (ml > len * MAST_OVERHANG) ms = cscale * len * MAST_OVERHANG / ml;
            float dx = c->mast_dx * len;
            inst_t mi = in;
            mi.x = r->x + fx * dx; mi.z = r->z + fz * dx;
            mi.y = -water + hh * c->deck;
            mi.scale = ms;
            scene_asset_instance(m, mi, r->key ^ 0x5BD1u, r->camdist);
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
            for (int i = 0; i < c->oar_pairs; ++i) {
                float t = c->oar_pairs > 1 ? i / (float)(c->oar_pairs - 1) - 0.5f : 0.f;
                float along = t * span;
                for (int side = -1; side <= 1; side += 2) {
                    inst_t oi = in;
                    oi.x = r->x + fx * along + sx * out * side;
                    oi.z = r->z + fz * along + sz * out * side;
                    oi.y = y;
                    oi.heading = r->heading + side * 1.5708f;   // blade outboard
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
