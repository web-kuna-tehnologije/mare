// Authored ship assembly (see shipyard.h, game/docs/DESIGN_SHIPS.md).
// Sections: the Gen I class table (rig layout + stats) · game ship type -> class · rigging,
// canvas and flags · submission through the authored-asset path.
// mare::game::view::Shipyard: the ship class table, the procedural rig/canvas meshes and the
// per-frame instance lists behind shipyard.h. The C ABI is a set of shims over the singleton at
// the bottom; the shared counters stay plain C globals.
#include "../platform/v3cpp.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern "C" {
#include "shipyard.h"
#include "render.h"
#include "scene.h"
#include "lod.h"
#include "../core/types.h"
#include "../core/util.h"
#include "../core/diag.h"
#include "../assets/asset.h"
#include "../data/tables.h"
#include "../assets/gjson.h"
#include "../world/wind.h"
}

// Shared counters (declared extern "C" in shipyard.h): plain globals, defined here outside the namespace.
int shipyard_enabled = 1;
int shipyard_oars_drawn, shipyard_ships_drawn, shipyard_fallbacks, shipyard_rigged, shipyard_guns_drawn;
int shipyard_flags_live, shipyard_yards_authored;

namespace mare::game::view {

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
#define OAR_PIVOT   0.26f       // 0.22.17: the rowlock, as a fraction of the oar's length inboard of its middle (issue14 "pivot in the ship part")

// SHIP_MAX_MASTS now lives in shipyard.h (0.22.23)

// What hangs from a mast. The reference sheet (assets/reference/fleet_sails.png) draws a square course
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
    int   sails;                        // 0 mixed (per ship, by her key and nation), 1 always new canvas, 2 always used (ship_classes.json "sails", 0.22.11); last, so the built-in rows need no change
    const char *gun;                    // 0.22.13 (issue10): the gun asset stood at every port (assets/ships/cannons -> gun_*); NULL = by class (gun_role_for)
    float gun_m;                        // its length in metres (0 = the asset's default)
} ship_class_t;

// The Gen I fleet as drawn in assets/reference/fleet_sails.png and the design sheets. Hull and mast
// models come in pairs from Meshy except the galiot, which borrows the generic Mediterranean
// lateen rig, and the great carrack, which sails on the war carrack's model stretched to its
// own length until its hull is generated.
//
//   stats: length, beam, draft (m), displacement (t), knots under sail, under oars, turn deg/s, guns, crew
static const ship_class_t builtin_classes[] = {
  { "coastal_trader", "coastal_trader_hull", "coastal_trader_masts", NULL,         21.f, .38f, .60f, 1.f,  1, 2,
    { .04f, -.33f },              { 1.f, .76f },             { 1, 2 },       { SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_single", "mast_mizzen" }, 0, 0, 0.f, .16f,
    { 21.f,  6.0f, 1.8f,   60.f, 5.0f, 0.f, 6.0f,  4,  15 } },
  { "cog",            "cog_hull",            "cog_masts",            NULL,         25.f, .38f, .60f, 1.f,  0, 1,
    { .02f },                     { 1.f },                   { 0 },          { SAIL_SQUARE1 }, { "mast_cog_single" },               0, 0, 0.f, .17f,
    { 25.f,  7.5f, 2.5f,  150.f, 4.5f, 0.f, 4.0f,  2,  20 } },
  { "armed_cog",      "armed_cog_hull",      "armed_cog_masts",      NULL,         32.f, .38f, .60f, 1.f,  0, 2,
    { .03f, -.33f },              { 1.f, .74f },             { 0, 0 },       { SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_two", "mast_mizzen" }, 0, 0, 0.f, .16f,
    { 32.f,  9.0f, 3.0f,  260.f, 4.5f, 0.f, 3.5f, 14,  70 } },
  { "hulk",           "hulk_hull",           "hulk_masts",           NULL,         35.f, .38f, .62f, 1.f,  1, 3,
    { .27f, -.01f, -.30f },       { .80f, 1.f, .78f },       { 0, 1, 2 },    { SAIL_SQUARE1, SAIL_SQUARE2, SAIL_SQUARE1 }, { "mast_cog_two", "mast_cog_two", "mast_cog_single" }, 0, 0, 0.f, .19f,
    { 35.f, 11.0f, 3.5f,  400.f, 4.0f, 0.f, 3.0f,  8,  50 } },
  { "carrack",        "carrack_hull",        "carrack_masts",        NULL,         38.f, .36f, .58f, 1.f,  1, 3,
    { .28f, -.01f, -.31f },       { .80f, 1.f, .90f },       { 0, 1, 2 },    { SAIL_SQUARE2, SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_two", "mast_carrack", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 38.f, 11.0f, 4.0f,  500.f, 5.0f, 0.f, 3.0f, 20,  90 } },
  { "war_carrack",    "war_carrack_hull",    "war_carrack_masts",    NULL,         47.f, .36f, .58f, 1.f,  1, 4,
    { .31f, -.03f, -.26f, -.42f },{ .82f, 1.f, .94f, .82f }, { 0, 1, 2, 2 }, { SAIL_SQUARE2, SAIL_SQUARE3, SAIL_LATEEN, SAIL_LATEEN }, { "mast_cog_two", "mast_carrack", "mast_mizzen", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 47.f, 13.0f, 5.0f, 1000.f, 4.5f, 0.f, 2.2f, 40, 250 } },
  { "great_carrack",  "war_carrack_hull",    "war_carrack_masts",    NULL,         56.f, .36f, .58f, 56.f / 47.f, 1, 4,
    { .31f, -.03f, -.26f, -.42f },{ .85f, 1.f, .96f, .84f }, { 0, 1, 2, 2 }, { SAIL_SQUARE2, SAIL_SQUARE3, SAIL_LATEEN, SAIL_LATEEN }, { "mast_cog_two", "mast_great_carrack", "mast_mizzen", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 56.f, 15.0f, 5.5f, 1500.f, 4.0f, 0.f, 1.8f, 74, 500 } },
  { "galiot",         "galiot_hull",         "mediterranean_masts",  "galley_oar", 28.f, .32f, .50f, 1.f,  0, 2,
    { .08f, -.30f },              { 1.f, .62f },             { 0, 0 },       { SAIL_LATEEN, SAIL_LATEEN }, { "mast_lateen_tall", "mast_lateen_tall" },  0, 22, .62f, .11f,
    { 28.f,  5.5f, 1.5f,  120.f, 5.0f, 6.5f, 5.0f,  6, 120 } },
  { "fusta",          "fusta_hull",          "fusta_masts",          "galley_oar", 25.f, .32f, .50f, 1.f,  1, 2,
    { .08f, -.30f },              { 1.f, .62f },             { 1, 2 },       { SAIL_LATEEN, SAIL_LATEEN }, { "mast_lateen_tall", "mast_lateen_tall" },  0, 20, .60f, .09f,
    { 25.f,  4.5f, 1.2f,   80.f, 5.5f, 7.0f, 6.0f,  4,  90 } },
  // 0.22.22 (issue18 "add real ship classes"): three more, built on hulls that are already
  // authored, as `great_carrack` has been since 0.22.4 - a caravel is a coaster's hull drawn
  // out and given a lateen mizzen; a xebec is a galiot lengthened, with a real battery; a
  // galleon is the armed cog's hull stretched past forty metres, which is roughly the line
  // the form actually took. No new art, and every authored hull in the tree is now sailed.
  { "caravel",        "coastal_trader_hull", "coastal_trader_masts", NULL,         24.f, .38f, .60f, 24.f / 21.f, 1, 2,
    { .06f, -.32f },              { 1.f, .80f },             { 1, 2 },       { SAIL_SQUARE2, SAIL_LATEEN }, { "mast_cog_single", "mast_mizzen" }, 0, 0, 0.f, .16f,
    { 24.f,  6.5f, 2.0f,   90.f, 5.6f, 0.f, 5.5f,  4,  25 } },
  { "xebec",          "galiot_hull",         "mediterranean_masts",  "galley_oar", 34.f, .32f, .50f, 34.f / 28.f, 0, 3,
    { .22f, -.02f, -.34f },       { .86f, 1.f, .66f },       { 0, 0, 0 },    { SAIL_LATEEN, SAIL_LATEEN, SAIL_LATEEN }, { "mast_lateen_tall", "mast_lateen_tall", "mast_lateen_tall" }, 0, 24, .62f, .11f,
    { 34.f,  7.5f, 2.2f,  200.f, 6.5f, 6.0f, 4.6f, 16, 110 } },
  { "galleon",        "armed_cog_hull",      "armed_cog_masts",      NULL,         42.f, .36f, .58f, 42.f / 32.f, 0, 3,
    { .29f, -.02f, -.33f },       { .84f, 1.f, .82f },       { 0, 0, 0 },    { SAIL_SQUARE2, SAIL_SQUARE3, SAIL_LATEEN }, { "mast_cog_two", "mast_carrack", "mast_mizzen" }, 1, 0, 0.f, .16f,
    { 42.f, 11.5f, 4.4f,  700.f, 4.8f, 0.f, 2.6f, 40, 220 } },
};
#define NBUILTIN ((int)(sizeof builtin_classes / sizeof builtin_classes[0]))
#define SHIP_CLASS_MAX 16

// The live class table (0.22.6, issue3 "can we add shipyard.cc data into files"). The built-in
// table above is the fallback - a tree without the data file still sails - and
// assets/ships/ship_classes.json overrides it entry by entry at startup, so the fleet can be
// retuned, and new classes added, without touching the source. Strings are copied into the
// store, because a loaded class cannot point at string literals.
typedef struct class_store_t {
    char name[32], hull[48], masts[48], oars[48], lib[SHIP_MAX_MASTS][48], gun[48];
} class_store_t;
static ship_class_t g_classes[SHIP_CLASS_MAX];
static class_store_t g_store[SHIP_CLASS_MAX];
static int g_nclasses = 0;
#define NCLASSES g_nclasses

static int sail_code_of(const gjson_t *j, int tok) {
    if (jstreq(j, tok, "square1")) return SAIL_SQUARE1;
    if (jstreq(j, tok, "square2")) return SAIL_SQUARE2;
    if (jstreq(j, tok, "square3")) return SAIL_SQUARE3;
    if (jstreq(j, tok, "lateen"))  return SAIL_LATEEN;
    return SAIL_NONE;
}
// Copy a JSON string into the class store and point the class at it; "" means "no such part",
// which the rest of the shipyard spells as NULL.
static const char *store_str(const gjson_t *j, int tok, char *dst, int cap, const char *dflt) {
    if (tok < 0) { if (!dflt) return NULL; snprintf(dst, cap, "%s", dflt); return dst; }
    jstrcpy(j, tok, dst, cap);
    return dst[0] ? dst : NULL;
}

// The fleet, from data (0.22.6). The built-in table is copied in first, then every entry of
// assets/ships/ship_classes.json overrides the class of the same name or appends a new one, so
// the file need only carry what it changes and a missing file costs nothing.
static void classes_init() {
    if (g_nclasses) return;
    for (int i = 0; i < NBUILTIN && i < SHIP_CLASS_MAX; ++i) g_classes[g_nclasses++] = builtin_classes[i];
    char path[600]; snprintf(path, sizeof path, "%s/ships/ship_classes.json", asset_root());
    FILE *f = fopen(path, "rb");
    if (!f) { LOG_INFO("asset", "no ship_classes.json beside the assets: the %d built-in classes stand", g_nclasses); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *js = (n > 0 && n < (1 << 20)) ? (char *)malloc((size_t)n + 1) : NULL;
    size_t got = js ? fread(js, 1, (size_t)n, f) : 0;
    fclose(f);
    if (!js) return;
    js[got] = 0;
    gjson_t j;
    // 0.22.13: gjson_parse returns 0 on success and -1 on failure (as gltf.cc and asset.cc read it); this
    // test had it backwards since 0.22.6, so ship_classes.json was never applied - every run logged
    // "is not valid JSON" over a valid file and sailed the built-in table. The data file is live now.
    if (gjson_parse(&j, js, (int)got) < 0) { LOG_WARN("asset", "%s is not valid JSON: the built-in classes stand", path); free(js); return; }
    int arr = jget(&j, 0, "classes");
    int changed = 0, added = 0;
    for (int e = 0, ne = jlen(&j, arr); e < ne; ++e) {
        int o = jat(&j, arr, e);
        char nm[32] = ""; jstrcpy(&j, jget(&j, o, "name"), nm, sizeof nm);
        if (!nm[0]) continue;
        int idx = -1;
        for (int i = 0; i < g_nclasses; ++i) if (g_classes[i].name && !strcmp(g_classes[i].name, nm)) { idx = i; break; }
        if (idx < 0) { if (g_nclasses >= SHIP_CLASS_MAX) { LOG_WARN("asset", "ship_classes.json: more than %d classes, '%s' skipped", SHIP_CLASS_MAX, nm); continue; }
                       idx = g_nclasses++; g_classes[idx] = ship_class_t{}; added++; }
        else changed++;
        ship_class_t *c = &g_classes[idx]; class_store_t *st = &g_store[idx];
        snprintf(st->name, sizeof st->name, "%s", nm); c->name = st->name;
        c->hull  = store_str(&j, jget(&j, o, "hull"),  st->hull,  sizeof st->hull,  c->hull);
        c->masts = store_str(&j, jget(&j, o, "masts"), st->masts, sizeof st->masts, c->masts);
        c->oars  = store_str(&j, jget(&j, o, "oars"),  st->oars,  sizeof st->oars,  c->oars);
        c->gun   = store_str(&j, jget(&j, o, "gun"),   st->gun,   sizeof st->gun,   c->gun);
        c->gun_m = (float)jnum(&j, jget(&j, o, "gun_m"), c->gun_m);
        c->length_m   = (float)jnum(&j, jget(&j, o, "length_m"), c->length_m);
        c->draft      = (float)jnum(&j, jget(&j, o, "draft"), c->draft);
        c->deck       = (float)jnum(&j, jget(&j, o, "deck"), c->deck);
        c->model_scale= (float)jnum(&j, jget(&j, o, "model_scale"), c->model_scale);
        c->split      = jint(&j, jget(&j, o, "split"), c->split);
        c->nmasts     = jint(&j, jget(&j, o, "nmasts"), c->nmasts);
        if (c->nmasts > SHIP_MAX_MASTS) c->nmasts = SHIP_MAX_MASTS;
        { int a = jget(&j, o, "mast_at"); if (a >= 0) jfloats(&j, a, c->mast_at, mini(jlen(&j, a), SHIP_MAX_MASTS)); }
        { int a = jget(&j, o, "mast_h");  if (a >= 0) jfloats(&j, a, c->mast_h,  mini(jlen(&j, a), SHIP_MAX_MASTS)); }
        { int a = jget(&j, o, "mast_part"); for (int k = 0, nk = a >= 0 ? mini(jlen(&j, a), SHIP_MAX_MASTS) : 0; k < nk; ++k) c->mast_part[k] = (signed char)jint(&j, jat(&j, a, k), c->mast_part[k]); }
        { int a = jget(&j, o, "sail");      for (int k = 0, nk = a >= 0 ? mini(jlen(&j, a), SHIP_MAX_MASTS) : 0; k < nk; ++k) c->sail[k] = (signed char)sail_code_of(&j, jat(&j, a, k)); }
        { int a = jget(&j, o, "mast_lib");  for (int k = 0, nk = a >= 0 ? mini(jlen(&j, a), SHIP_MAX_MASTS) : 0; k < nk; ++k) c->mast_lib[k] = store_str(&j, jat(&j, a, k), st->lib[k], sizeof st->lib[k], c->mast_lib[k]); }
        c->spritsail = jint(&j, jget(&j, o, "spritsail"), c->spritsail);
        { int sv = jget(&j, o, "sails");   // "mixed" | "new" | "used" (0.22.11, todo 17: the sail state as data)
          if (sv >= 0) c->sails = jstreq(&j, sv, "new") ? 1 : jstreq(&j, sv, "used") ? 2 : 0; }
        c->oar_pairs = jint(&j, jget(&j, o, "oar_pairs"), c->oar_pairs);
        c->oar_span  = (float)jnum(&j, jget(&j, o, "oar_span"), c->oar_span);
        c->beam_frac = (float)jnum(&j, jget(&j, o, "beam_frac"), c->beam_frac);
        int so = jget(&j, o, "stats");
        if (so >= 0) {
            c->st.length_m   = (float)jnum(&j, jget(&j, so, "length_m"), c->st.length_m);
            c->st.beam_m     = (float)jnum(&j, jget(&j, so, "beam_m"), c->st.beam_m);
            c->st.draft_m    = (float)jnum(&j, jget(&j, so, "draft_m"), c->st.draft_m);
            c->st.tonnes     = (float)jnum(&j, jget(&j, so, "tonnes"), c->st.tonnes);
            c->st.knots     = (float)jnum(&j, jget(&j, so, "knots_sail"), c->st.knots);
            c->st.oar_knots = (float)jnum(&j, jget(&j, so, "knots_oar"), c->st.oar_knots);
            c->st.turn_dps   = (float)jnum(&j, jget(&j, so, "turn_dps"), c->st.turn_dps);
            c->st.guns       = jint(&j, jget(&j, so, "guns"), c->st.guns);
            c->st.crew       = jint(&j, jget(&j, so, "crew"), c->st.crew);
        }
    }
    LOG_INFO("asset", "ship classes from %s: %d overridden, %d added, %d in the fleet", path, changed, added, g_nclasses);
    free(j.t); free(js);
}

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

// Cordage is *physical* now (0.19): a shroud on a 47 m carrack is a 4 cm rope, which in the
// hull's own units is under a thousandth of its length. Drawn as line segments the geometry
// stage (meshes/rope.geom) widens to that size on screen, never under ~0.6 px, so the rig is
// hemp at battle scale and a line from the regional view instead of the 37 cm black poles the
// fixed-width quads made of it in the staged battle. Without a geometry stage the old cross
// quads stand in, thinner than before.
#define RIG_ROPE_W   0.0009f        // rope half-width, fractions of hull length (physical)
#define RIG_ROPE_W_FALLBACK 0.0020f // cross-quad half-width without a geometry stage
#define RIG_YARD_W   0.0050f        // procedural spar half-width (the far stand-in for an authored yard)
#define RIG_SAIL_OFF 0.015f         // canvas hangs this far forward of its mast (on the yard's fore side)
// 0.22.17 (issue13's reference paintings): a great ship flew a *streamer* from her truck -
// several times her beam, tapering to a swallow tail - not a handkerchief. The cloth is the same
// verlet grid; the mesh tapers it and forks its fly.
#define RIG_FLAG_LEN 0.22f          // along the fly, fractions of hull length (was 0.085)
#define RIG_FLAG_H   0.024f         // at the hoist; the fly tapers to RIG_FLAG_TAPER of it
#define RIG_FLAG_TAPER 0.30f
#define RIG_FLAG_FORK  0.26f        // how far the middle of the fly is cut back
static const float rope_col[3] = { 0.20f, 0.155f, 0.115f };  // tarred hemp: dark brown, lit, not black
// Two conditions of cordage (0.22.6, issue3 "add 2 types of rigging premium and used"): a
// ship fresh from the arsenal has pale new hemp on her, a worn one dark tarred rope gone
// hairy. `g_rope_col` is what rig_rope lays down and rig_build sets it per copy.
static const float rope_new[3]  = { 0.44f, 0.37f, 0.26f };
static const float rope_worn[3] = { 0.17f, 0.135f, 0.10f };
static const float *g_rope_col = rope_col;
static const float spar_col[3] = { 0.36f, 0.27f, 0.17f };     // oiled oak
static const float sail_col[3] = { 0.86f, 0.83f, 0.74f };     // unbleached canvas
static const float nrm_x[3] = { 1, 0, 0 }, nrm_z[3] = { 0, 0, 1 };

// The lantina as the authored asset models it (assets/elm/lateen_yard: a 45-degree spar slung
// at its middle, low end forward after the import's -x turn). The unit lateen canvas is cut to
// hang from exactly this line, in mast-height units with the mast axis at x = 0 and the deck at
// y = 0, so canvas and authored yard coincide by construction when both are instanced on the
// same swung heading.
// 0.22: the tack was 0.56 mast-heights forward of its mast and only 0.08 above the deck. On a
// galiot that put the fore end of the yard past the stem and swept the canvas *through* the
// bulwarks every time the sail was swung. The line is now shorter forward and carried higher,
// which is also how a settee yard is actually slung - the tack is hauled down to the stemhead,
// not to the waterline.
// 0.22.6 (issue3 "divide lanteen from front mast"): the tack reached 0.44 mast-heights
// forward, which on a four-master put the fore end of the mizzen's yard among the mainmast's
// rigging. It is hauled in to 0.30 and carried higher; the peak goes further aft and up, so
// the spar keeps its length and its rake and the whole sail sits abaft its own mast.
// 0.22.13 (issue10 "lateen and rigging to the top of the mast, not above"): the peak sits AT the
// masthead (1.0 of the mast standing above the deck - shipyard_push measures the mast from
// its heel, MAST_STEP below the deck, and hands the visible height here), the tack is carried
// further forward and lower, so the spar is a tenth longer than before while nothing of it
// stands over the truck. The clew and foot move with it (LAT_CLEW_*, LAT_FOOT_*).
#define LAT_TACK_X  0.40f           // low forward end (the tack)
#define LAT_TACK_Y  0.30f           // 0.22.17 (issue13/14 "triangular sails ... don't clip through ships"): 0.16 -> 0.30
#define LAT_PEAK_X -0.62f           // high aft end (the peak, at the masthead)
#define LAT_PEAK_Y  0.98f
#define LAT_FOOT_X  0.48f           // the tack corner of the canvas
#define LAT_FOOT_Y  0.27f           // 0.22.17: the whole triangle rides a seventh of a mast higher
#define LAT_CLEW_X -0.45f           // the clew, sheeted aft
#define LAT_CLEW_Y  0.33f
// The canvas is bent to the spar with robands - short lashings every few feet along the head -
// and its foot is guided by a rope from tack to clew. Both are drawn in the sail's own mesh
// (0.22.6, issue3), so they swing with it.
#define LAT_ROBANDS 7               // lashings along the head
#define LAT_HEAD_GAP 0.004f         // how far under the spar the head sits, in mast heights (was 0.014)
// Below this projected size an authored yard would be drawn from a decimated far LOD that has
// no thickness left (a thin spar decimates to a sliver): the sail's own procedural spar takes
// over, and the hand-over is a few pixels wide either way.
#define YARD_ASSET_MIN_PX 44.f

// The square yards split off the mast library (tools/blender_prepare_part.py --pivot bbox,
// assets/elm/yard_*): one set per mast type - course, topsail, topgallant - each normalised to
// unit length across the ship (descriptor: forward +z, fit width 1). The generic square_yard
// stands in where a set has nothing for a layer.
typedef struct yard_set_t { const char *mast; const char *yard[3]; } yard_set_t;
static const yard_set_t yard_sets[] = {
    { "mast_cog_single",    { "yard_cog_single",     "yard_cog_top",     "yard_cog_top" } },
    { "mast_cog_two",       { "yard_cog_course",     "yard_cog_top",     "yard_cog_top" } },
    { "mast_carrack",       { "yard_carrack_course", "yard_carrack_top", "yard_carrack_top" } },
    { "mast_great_carrack", { "yard_great_course",   "yard_great_main",  "yard_great_top" } },
};

// Flags are simulated (0.19): a verlet cloth per ship, pinned along its hoist to the main
// masthead, in the ship's own frame (hull fractions) so the mesh shader's roll, pitch and
// swell carry it with the mast. Gravity and the apparent wind are real metres per second
// scaled by the class's length; what the player sees is a flag of that size streaming,
// drooping and snapping as the wind and the ship's way change.
#define FLAG_W     9                // particles along the fly
#define FLAG_H     5                // particles along the hoist
#define FLAG_MAX   48               // simulated flags: the nearest rigged ships, LRU beyond that
#define FLAG_WIND_MS 7.5f           // metres per second of a unit wind strength
#define FLAG_WAY_MS  3.2f           // the ship's own way at full `rock`
#define FLAG_SUBSTEPS 2
typedef struct flag_sim_t {
    unsigned key; float last_seen;  // ship key (shipyard_req_t.key) and RF.time it was last pushed
    int   inited, drawn;
    float p[FLAG_H][FLAG_W][3], pp[FLAG_H][FLAG_W][3];
    float step_t;                   // RF.time of the last integration
    mesh_t mesh; int built;
    inst_t inst;                    // this frame's rig instance (the flag rides the rig's transform)
} flag_sim_t;

// ---------------------------------------------------------------------------
// The cordage, actually simulated (0.22.23, todo 121b - "a verlet chain for the sheets and
// braces", asked for in issue16, issue17 and now issue19).
//
// What this is, exactly, so nobody has to guess: a **four-node verlet chain per ship**, hung
// from her masthead in her own frame and integrated on the CPU - position Verlet with three
// relaxation passes on the distance constraints, which is the standard scheme. It is *not* a
// separate chain per rope: her whole rig shares one solved chain, and each rope takes its
// shape from it scaled by its own slack. That is a deliberate and defensible simplification -
// every rope on one ship hangs in the same gravity and swings in the same accelerations, so
// they really do move together - and it is what lets the rig stay a single per-class mesh
// instead of needing per-ship geometry, which is the wall this item hit for four rounds.
//
// The chain is driven by what actually moves cordage:
//   * apparent gravity in her frame, which tilts with her heel (g sin roll across the deck),
//   * the angular whip of her roll - the masthead's lateral acceleration is what throws the
//     bight out and is why rigging swings *after* the ship rather than with her,
//   * the wind across the deck.
// Because it is integrated rather than evaluated, it lags, overshoots and settles on its own:
// a ship that rolls hard sees her rigging swing out, come back past the vertical and damp
// down over a few seconds, which is the behaviour a canned sine cannot produce at all.
#define RIG_NODES    4
#define RIG_SIM_MAX  96          // ships whose cordage is solved; the rest hang straight
#define RIG_SEG      0.25f       // segment length of the model chain (it is normalised)
#define RIG_DAMP     0.985f      // verlet damping per step: hemp is not a pendulum in vacuum
#define RIG_G        9.0f

typedef struct rig_sim_t {
    unsigned key; int used, inited;
    float x[RIG_NODES], y[RIG_NODES];        // current, ship frame: +x to starboard, +y DOWN
    float ox[RIG_NODES], oy[RIG_NODES];      // previous (verlet keeps no velocity of its own)
    float last_roll, roll_v, t_seen;
} rig_sim_t;

static rig_sim_t g_rig_sim[RIG_SIM_MAX];

// solve this ship's chain forward to now, and return her swing angle (radians, + to starboard)
static void rig_sim_step(unsigned key, float roll, float wind_across, float dt, float *out_swing, float *out_taut) {
    rig_sim_t *S = NULL, *lru = &g_rig_sim[0];
    for (int i = 0; i < RIG_SIM_MAX; ++i) {
        if (g_rig_sim[i].used && g_rig_sim[i].key == key) { S = &g_rig_sim[i]; break; }
        if (!g_rig_sim[i].used) { lru = &g_rig_sim[i]; break; }
        if (g_rig_sim[i].t_seen < lru->t_seen) lru = &g_rig_sim[i];
    }
    if (!S) { S = lru; S->used = 1; S->key = key; S->inited = 0; }
    S->t_seen = (float)RF.anim_time;
    if (!S->inited) {                                   // born hanging straight down
        S->inited = 1; S->last_roll = roll; S->roll_v = 0.f;
        for (int i = 0; i < RIG_NODES; ++i) { S->x[i] = S->ox[i] = 0.f; S->y[i] = S->oy[i] = RIG_SEG * i; }
    }
    if (dt <= 0.f) { *out_swing = atan2f(S->x[RIG_NODES-1], maxf(S->y[RIG_NODES-1], 1e-3f)); *out_taut = 0.f; return; }
    dt = clampf(dt, 0.001f, 0.05f);                     // a long frame must not explode the chain
    // her roll rate and its change: the masthead's lateral whip
    float rv = (roll - S->last_roll) / dt; S->last_roll = roll;
    float ra = (rv - S->roll_v) / dt; S->roll_v = rv;
    ra = clampf(ra, -40.f, 40.f);
    // the forces, in her frame: gravity tilted by her heel, the whip of the roll, the wind
    float ax = RIG_G * sinf(roll) - ra * 0.45f + wind_across * 2.2f;
    float ay = RIG_G * cosf(roll);
    for (int i = 1; i < RIG_NODES; ++i) {               // node 0 is the masthead and does not move
        float nx = S->x[i] + (S->x[i] - S->ox[i]) * RIG_DAMP + ax * dt * dt;
        float ny = S->y[i] + (S->y[i] - S->oy[i]) * RIG_DAMP + ay * dt * dt;
        S->ox[i] = S->x[i]; S->oy[i] = S->y[i];
        S->x[i] = nx; S->y[i] = ny;
    }
    for (int pass = 0; pass < 3; ++pass)                // relax the distance constraints
        for (int i = 0; i < RIG_NODES - 1; ++i) {
            float dx = S->x[i+1] - S->x[i], dy = S->y[i+1] - S->y[i];
            float d = sqrtf(dx * dx + dy * dy); if (d < 1e-5f) { dy = RIG_SEG; d = RIG_SEG; }
            float corr = (d - RIG_SEG) / d * 0.5f;
            float cx = dx * corr, cy = dy * corr;
            if (i > 0) { S->x[i] += cx; S->y[i] += cy; }           // the masthead end is pinned
            else { S->x[i+1] -= cx * 2.f; S->y[i+1] -= cy * 2.f; continue; }
            S->x[i+1] -= cx; S->y[i+1] -= cy;
        }
    *out_swing = atan2f(S->x[RIG_NODES-1], maxf(S->y[RIG_NODES-1], 1e-3f));
    // how far the chain is drawn out towards straight: a rope swung hard is also pulled taut
    float span = sqrtf(S->x[RIG_NODES-1] * S->x[RIG_NODES-1] + S->y[RIG_NODES-1] * S->y[RIG_NODES-1]);
    *out_taut = clampf((span / (RIG_SEG * (RIG_NODES - 1)) - 0.92f) / 0.08f, 0.f, 1.f);
}

typedef struct rig_cache_t { mesh_t rope, rope_worn, cloth; int built; unsigned gen; } rig_cache_t;
// Square canvas as shared unit meshes (half-width 1, head at y = 0, hanging down), one per
// cut: the deep single course of a cog, the standard course, the narrower topsails. Instanced
// per (ship, station, layer) on the yard's braced heading, so canvas and the authored
// square_yard turn together about the mast - the lantina's model, applied to the square rig.
enum { SQ_DEEP = 0, SQ_COURSE, SQ_TOP, SQ_KINDS };
static const float sq_drop[SQ_KINDS] = { 1.67f, 1.05f, 1.28f };
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

class Shipyard {
public:
    int  push(const shipyard_req_t *r);
    void request_all(void);
    void frame_begin(void);
    void upload(void);
    int  submit(void);
    int  submit_shadow(void);
    const char *class_name(int type, int nation);
    int  class_stats(int type, int nation, ship_stats_t *out);

private:
    static void rig_bar(mb_t *m, const float a[3], const float b[3], float w, const float *col);
    static void rig_spar(mb_t *m, const float a[3], const float b[3], float w, const float *col);
    static void rig_rope(mb_t *m, const float a[3], const float b[3], float w);
    static int  cloth_level(float t);
    static void rig_sail_square(mb_t *cloth, mb_t *spars, float x, float top, float bot, float hw);
    static void rig_sail_lateen(mb_t *cloth, mb_t *spars, float sx, float dy, float h);
    static void rig_build_cordage(mb_t *rope, mb_t *cloth, const ship_class_t *c, int nm,
                                  const float *sx, const float *ty, float dy, float hb, float bow, float stern, float castle);
    static void rig_build(rig_cache_t *rc, const ship_class_t *c, int nm, const float *sx,
                          const float *ty, float dy, float hb, float bow, float stern, float castle);
    static asset_t *yard_asset(const ship_class_t *c, int k, int layer);
    flag_sim_t *flag_slot(unsigned key);
    void flag_step(flag_sim_t *f, float staff_x, float staff_y, float g_local, float wl_x, float wl_z, float dt);
    void flag_push(const shipyard_req_t *r, const ship_class_t *c, float staff_x, float staff_y,
                   float wl_x, float wl_z, const inst_t *ri);
    void flag_build_mesh(flag_sim_t *f, float r, float g, float b);
    static const ship_class_t *by_name(const char *n);
    static int  med_flavour(int nation);
    static int  north_flavour(int nation);
    static const ship_class_t *class_for(int type, int nation);
    static int  ship_quality(const shipyard_req_t *r);
    static asset_t *mast_asset(const ship_class_t *c, int k);

    // ---- state (file-scope statics of the C version)
    rig_cache_t rig_cache[SHIP_CLASS_MAX];
    instlist_t  rig_inst[SHIP_CLASS_MAX], rig_worn_inst[SHIP_CLASS_MAX], cloth_inst[SHIP_CLASS_MAX];
    int         rope_lines = -1;      // ropes as geometry-stage lines (meshes_rope_available), decided on first use
    // The lateen canvas as one shared unit mesh (mast height 1, mast axis at the origin),
    // instanced per lateen station with the yard's swung heading, so sail and lantina turn as one
    // piece about the mast. Built lazily on the render thread, like the class rigs. Two cuts
    // each: [0] canvas alone (an authored yard stands at the head), [1] canvas with its own
    // procedural spar (the far stand-in once the authored yard would be a sliver).
    mesh_t     lateen_cloth_mesh[2]; int lateen_cloth_built;
    instlist_t lateen_cloth_inst[2];
    mesh_t     sq_cloth_mesh[SQ_KINDS][2]; int sq_cloth_built;
    instlist_t sq_cloth_inst[SQ_KINDS][2];
    flag_sim_t flags[FLAG_MAX];
};

Shipyard &shipyard() { static Shipyard s; return s; }


// one rope or spar as a cross of two double-sided quads: a single quad vanishes edge-on, and a
// tube would cost twenty times as much for something a few pixels wide
void Shipyard::rig_bar(mb_t *m, const float a[3], const float b[3], float w, const float *col) {
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
// A spar as a solid: an eight-sided prism from a to b, half-width w at the middle tapering to
// 0.55 w at the ends (0.22.14, issue11 "fix this lateen": the lantina is this, built in the
// sail's own mesh on the exact line the canvas is bent to, so yard and head can never part).
void Shipyard::rig_spar(mb_t *m, const float a[3], const float b[3], float w, const float *col) {
    float d[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
    float dl = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    if (dl < 1e-5f) return;
    d[0] /= dl; d[1] /= dl; d[2] /= dl;
    float up[3] = { 0, 1, 0 }; if (fabsf(d[1]) > 0.95f) { up[0] = 1; up[1] = 0; }
    float u[3] = { d[1]*up[2] - d[2]*up[1], d[2]*up[0] - d[0]*up[2], d[0]*up[1] - d[1]*up[0] };
    float ul = sqrtf(u[0]*u[0] + u[1]*u[1] + u[2]*u[2]); if (ul < 1e-5f) return;
    u[0] /= ul; u[1] /= ul; u[2] /= ul;
    float v[3] = { d[1]*u[2] - d[2]*u[1], d[2]*u[0] - d[0]*u[2], d[0]*u[1] - d[1]*u[0] };
    const int S = 8, R = 4;                       // sides, rings along the length
    float ring[R + 1][S][3];
    for (int r = 0; r <= R; ++r) {
        float t = (float)r / R;
        float taper = 0.55f + 0.45f * sinf(3.14159f * t);   // thickest amidships, slim at the ends
        for (int s = 0; s < S; ++s) {
            float ang = (float)s / S * 6.2831853f, cs = cosf(ang) * w * taper, sn = sinf(ang) * w * taper;
            for (int k = 0; k < 3; ++k) ring[r][s][k] = a[k] + d[k] * dl * t + u[k] * cs + v[k] * sn;
        }
    }
    for (int r = 0; r < R; ++r) for (int s = 0; s < S; ++s) {
        int s1 = (s + 1) % S;
        float sh = 0.86f + 0.14f * (0.5f + 0.5f * cosf((float)s / S * 6.2831853f + 0.6f));   // a little lathe shading per facet
        mb_quad(m, ring[r][s], ring[r][s1], ring[r + 1][s1], ring[r + 1][s], col[0] * sh, col[1] * sh, col[2] * sh, 0.f);
    }
}
// standing rigging: a line pair for the geometry stage, or the old cross quads without one.
// `w` is the physical half-width in hull fractions (the fallback uses its own, visible width).
// 0.22.17 (issue14): 1 = standing rigging, 2 = running - a sheet or brace the drawing canvas
// hauls taut and that goes slack in a calm (meshes/rope.vert reads the kind off the vertex).
static float g_rope_kind = 1.f;

void Shipyard::rig_rope(mb_t *m, const float a[3], const float b[3], float w) {
    // 0.22.15 (issue12 "rope behaviour flexible and physics based"): eight segments with a slack
    // that grows with the rope's length and how level it lies - a stay hauled taut sags a little,
    // a long near-horizontal sheet a lot; rope.vert hangs the catenary and swings it with the
    // wind and against the heel
    if (meshes_rope_available()) {
        float dx = b[0]-a[0], dy = b[1]-a[1], dz = b[2]-a[2], L = sqrtf(dx*dx + dy*dy + dz*dz);
        float level = L > 1e-5f ? 1.f - fabsf(dy) / L : 0.f;              // 1 = horizontal
        float slack = L * (0.020f + 0.130f * level * level);   // 0.22.17 (issue14 "more elasticity"): half as much sag again, and the shader works it harder
        mb_line_rope(m, a, b, w, slack, g_rope_kind, 8, g_rope_col[0], g_rope_col[1], g_rope_col[2]);
    }
    else rig_bar(m, a, b, RIG_ROPE_W_FALLBACK * (w / RIG_ROPE_W), g_rope_col);
}

// How free a strip boundary is, as the 32-level code: t = 0 at the spar, 1 at the loose edge.
int Shipyard::cloth_level(float t) { int l = (int)(t * 31.f + 0.5f); return l < 0 ? 0 : l > 31 ? 31 : l; }

// A square sail bent to a yard at `top`, hanging to `bot`, half-width `hw`, its plane at x.
// Built as a grid so the belly can be shaped between the leeches as well as from head to foot.
// spars == NULL: an authored yard stands there (the square_yard asset, or the bowsprit's own
// hanging yard), so no procedural bar is drawn - the canvas alone.
void Shipyard::rig_sail_square(mb_t *cloth, mb_t *spars, float x, float top, float bot, float hw) {
    const int N = 6, M = 4;
    float ya[3] = { x, top, -hw * 1.06f }, yb[3] = { x, top, hw * 1.06f };
    if (spars) rig_bar(spars, ya, yb, maxf(RIG_YARD_W, 0.04f * hw), spar_col);
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
    // 0.22.14 ("add ropes to the bottom of the sails"): the foot is roped - a bolt-rope from clew
    // to clew - and a sheet leads from each clew down and aft towards the deck, with a short
    // clew-line up the leech. In the sail's own mesh, so they brace and swing with it.
    {
        float rw = maxf(0.0045f * hw, RIG_ROPE_W_FALLBACK * 0.6f);
        float cl[3] = { x, bot, -hw * 1.0f }, cr[3] = { x, bot, hw * 1.0f };
        rig_spar(cloth, cl, cr, rw * 0.8f, rope_col);
        for (int side = -1; side <= 1; side += 2) {
            float clew[3] = { x, bot, side * hw * 1.0f };
            float sheet[3] = { x - 0.55f * hw, bot - 0.75f * hw, side * hw * 0.80f };      // down and aft, inboard a little: made fast on the deck
            rig_spar(cloth, clew, sheet, rw * 1.3f, rope_col);
            float leech[3] = { x, bot + (top - bot) * 0.35f, side * hw * 0.96f };          // the clew-line up the leech
            rig_spar(cloth, clew, leech, rw * 0.7f, rope_col);
        }
    }
}

// A lateen: a yard raked from low forward (the tack) to high aft (the peak, above the
// masthead), the canvas hanging from it to the clew near the deck aft - cut with a short luff
// at the tack, as the settee rig the galleys actually carried. `h` is the mast height; the
// sail is sized by it. Bellies sideways (its normal is +z), sheeted to one side of the mast.
void Shipyard::rig_sail_lateen(mb_t *cloth, mb_t *spars, float sx, float dy, float h) {
    // spars == NULL: the authored lantina stands on this line (shipyard_push), canvas only
    const int N = 5, M = 6;
    float z = 0.012f;
    // The robands go into whichever mesh is being built - but they must be *triangles*: this
    // mesh is drawn by the triangle pipeline, and rig_rope emits two-vertex line primitives
    // for the geometry stage when one is available. Feeding those to a triangle draw turned
    // every lateen into a black sheet (0.22.6). rig_bar is the cross-quad form.
    mb_t *cord = cloth;   // 0.22.14: the robands and the spar live with the canvas in every cut
    const float roband_w = RIG_ROPE_W_FALLBACK * 0.55f;
    // The yard is the authored asset's line (LAT_*): a 45-degree spar from low at the bow -
    // reaching well forward of the mast - to the peak above the masthead. The settee-cut
    // triangle hangs just under it, tack near the rail, clew sheeted aft.
    float ya[3] = { sx + LAT_TACK_X * h, dy + LAT_TACK_Y * h, 0 }, yb[3] = { sx + LAT_PEAK_X * h, dy + LAT_PEAK_Y * h, 0 };
    (void)spars; rig_spar(cloth, ya, yb, maxf(RIG_YARD_W, 0.0060f * h), spar_col);   // 0.22.14: the lantina itself, a tapered prism on the head's line (issue11 "fix this lateen and sail")
    float T[3] = { ya[0] + (yb[0] - ya[0]) * 0.04f, ya[1] + (yb[1] - ya[1]) * 0.04f - LAT_HEAD_GAP * h, z };   // head, inset from the ends, right under the spar
    float P[3] = { ya[0] + (yb[0] - ya[0]) * 0.97f, ya[1] + (yb[1] - ya[1]) * 0.97f - LAT_HEAD_GAP * h, z };
    // the foot: sheeted well above the rail, so a swung sail passes over the deck instead of
    // through it (0.22)
    float F[3] = { sx + LAT_FOOT_X * h, dy + LAT_FOOT_Y * h, z }, C[3] = { sx + LAT_CLEW_X * h, dy + LAT_CLEW_Y * h, z };
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
    // Robands (0.22.6): the head is lashed to the spar every few feet. Each is a short rope
    // from the yard down to the head of the canvas - what makes the sail read as *bent to* the
    // lantina rather than hanging near it.
    for (int i = 0; i <= LAT_ROBANDS; ++i) {
        float t = (float)i / LAT_ROBANDS;
        float yp[3], hp[3];
        for (int a = 0; a < 3; ++a) { yp[a] = ya[a] + (yb[a] - ya[a]) * t; hp[a] = T[a] + (P[a] - T[a]) * t; }
        rig_spar(cord, yp, hp, roband_w * h, rope_col);
    }
    // the foot rope: tack to clew, guiding the bottom of the sail (issue3)
    rig_spar(cord, F, C, roband_w * 1.3f * h, rope_col);
    // the sheet: from the clew down and aft to the rail, and the tack hauled down forward
    { float sheet[3] = { sx - 0.54f * h, dy + 0.02f * h, z };
      rig_spar(cord, C, sheet, roband_w * 1.4f * h, rope_col);
      float tackpt[3] = { sx + 0.44f * h, dy + 0.02f * h, z };
      rig_spar(cord, F, tackpt, roband_w * 1.4f * h, rope_col); }
}

// Build both meshes for a class. `hb` is the hull's half beam and `ty[]` the mast tops, all in
// fractions of hull length measured from the waterline.
// The standing rigging of one class. Split out of rig_build (0.22.6) so the cordage can be
// laid twice, once in each condition: `cloth` is built only on the first pass.
void Shipyard::rig_build_cordage(mb_t *rope, mb_t *cloth, const ship_class_t *c, int nm,
                                 const float *sx, const float *ty, float dy, float hb, float bow, float stern, float castle) {
    // 0.22.14 (issue11 "remove external riggings"): bow and stern are the hull model's own ends
    // (were 0.47 of the length, past the counter of every hull, so the backstays ran down into the
    // sea), and the feet of the aft masts' stays and shrouds stand on the castle, not the waist
    int main = 0;
    for (int k = 1; k < nm; ++k) if (c->mast_h[k] > c->mast_h[main]) main = k;
    for (int k = 0; k < nm; ++k) {
        float lift = nm >= 3 && c->mast_at[k] <= -0.22f ? castle : 0.f;   // the castle under the aft stations (shipyard_push stn_lift)
        // 0.22.13 (issue10): ty[] is the real masthead now (shipyard_push measures each station's own
        // asset from its heel); the standing rigging is made fast at the hounds, a little under it
        float top[3] = { sx[k], dy + (ty[k] - dy) * 0.965f, 0 };
        // shrouds: three a side from the masthead to the channels, raked a little aft
        for (int i = 0; i < 3; ++i) {
            float f = (i - 1) * 0.055f;
            for (int side = -1; side <= 1; side += 2) {
                float foot[3] = { sx[k] + f - 0.02f, dy + 0.01f + lift, side * hb * (0.86f - 0.06f * i) };   // to the channels at the rail
                rig_rope(rope, top, foot, RIG_ROPE_W);
            }
        }
        // forestay to the next mast's foot (or the bow), backstay to the stern: the stays are
        // the heaviest cordage aboard
        // 0.22.17 (issue14 "remove riggings on the stern that lead nowhere"): the stays were
        // picked by *array index*, and the stations run bow to stern, so every mast's "forward"
        // stay went to the stem and the foremast's "after" stay ran the whole length of the ship
        // to the taffrail, through the main and mizzen tops - the rope to nowhere. They are
        // picked by *station* now: forward to the next mast ahead or the stem, aft to the next
        // mast astern or (for the aftermost only) the taffrail.
        int kf = -1, ka = -1;
        for (int q = 0; q < nm; ++q) {
            if (q == k) continue;
            if (sx[q] > sx[k] && (kf < 0 || sx[q] < sx[kf])) kf = q;     // the nearest mast forward
            if (sx[q] < sx[k] && (ka < 0 || sx[q] > sx[ka])) ka = q;     // ... and astern
        }
        float fwd[3] = { kf >= 0 ? sx[kf] : bow,
                         kf >= 0 ? dy + (ty[kf] - dy) * 0.18f : dy + 0.012f, 0 };
        rig_rope(rope, top, fwd, RIG_ROPE_W * 1.35f);
        float aft[3] = { ka >= 0 ? sx[ka] : stern,
                         ka >= 0 ? dy + (ty[ka] - dy) * 0.18f : dy + 0.012f + lift, 0 };
        rig_rope(rope, top, aft, RIG_ROPE_W * 1.35f);
        // ratlines on the main shrouds, so the rig reads as climbable up close
        if (k == main) {
            for (int r = 1; r <= 5; ++r) {
                float t = r / 6.f, y = dy + (ty[k] - dy) * t * 0.72f;
                float w = hb * 0.86f * (1.f - t * 0.72f);
                float ax = sx[k] - 0.02f - 0.055f * (1.f - t);
                for (int side = -1; side <= 1; side += 2) {
                    float p0[3] = { ax, y, side * w * 0.55f }, p1[3] = { ax, y, side * w };
                    rig_rope(rope, p0, p1, RIG_ROPE_W * 0.6f);
                }
            }
        }
        // Canvas is not built here any more: every sail - square and lateen - is a per-ship
        // instance on its yard's braced heading (shipyard_push), hanging from the authored
        // square_yard / lateen_yard assets. Only the standing rigging stays in this mesh.
        // 0.22.12 (issue9 "add rope connecting sails to boat"): the running rigging that ties
        // the canvas to the hull - a sheet from each clew of every square sail down and aft to
        // the rail, and the lateen's sheet from the clew to the quarter. The canvas swings on
        // its brace and the sheet is laid for the squared yard, so at a hard brace a sheet
        // lands a little off the clew; from the deck it still reads as the sail made fast.
        {
            g_rope_kind = 2.f;   // 0.22.17: the sheets below are running rigging
            float mh = ty[k] - dy;
            int code = c->sail[k];
            if (code == SAIL_LATEEN) {
                float clew[3] = { sx[k] + LAT_CLEW_X * mh, dy + LAT_CLEW_Y * mh, 0.012f };
                for (int side = -1; side <= 1; side += 2) {
                    float rail[3] = { sx[k] - 0.52f * mh, dy + 0.012f, side * hb * 0.92f };
                    rig_rope(rope, clew, rail, RIG_ROPE_W * 1.1f);
                }
            } else if (code >= SAIL_SQUARE1 && code <= SAIL_SQUARE3) {
                const sq_layout_t *ly = &sq_layout[code];
                for (int l = 0; l < ly->n; ++l) {
                    float top = dy + mh * ly->l[l].top, hw = mh * ly->l[l].hw;
                    float foot = top - sq_drop[(int)ly->l[l].kind] * hw;
                    for (int side = -1; side <= 1; side += 2) {
                        float clew[3] = { sx[k] + RIG_SAIL_OFF, foot, side * hw };
                        // the course sheets lead aft to the rail; a topsail's sheets lead to the
                        // yard below it (here: down to the course's head), as on a real rig
                        float to[3] = { l == 0 ? sx[k] - 0.16f * mh : sx[k] + RIG_SAIL_OFF, l == 0 ? dy + 0.012f : dy + mh * ly->l[l - 1].top, l == 0 ? side * hb * 0.92f : side * mh * ly->l[l - 1].hw };
                        rig_rope(rope, clew, to, RIG_ROPE_W * (l == 0 ? 1.1f : 0.8f));
                    }
                }
            }
        }
        g_rope_kind = 1.f;
    }
    if (c->spritsail) {   // the bowsprit, steeved up from the forecastle, with its spritsail slung under it
        if (!asset_find("bowsprit")) {   // authored spar in the registry: shipyard_push stands it instead
            float a[3] = { bow - 0.04f, dy + 0.05f, 0 }, b[3] = { bow + 0.20f, dy + 0.16f, 0 };
            rig_bar(rope, a, b, RIG_YARD_W, spar_col);
        }
        // The spritsail (0.22.9, issue6 "front lantent issue sail.png"). It was cut as a tall
        // narrow panel whose head stood *above* the bowsprit and whose foot hung down to the
        // water: from the beam that is the sliver of canvas trailing in the sea the screenshot
        // circles. A spritsail is the opposite shape - a shallow square sail, nearly twice as
        // wide as it is deep, bent to a yard slung *under* the bowsprit (which stands at
        // dy + 0.10, shipyard_push) with its foot well clear of the sea.
        // 0.22.13: it was 0.31 of the hull wide and a twentieth deep - from abeam a white bar lying across
        // the bow (issue10 battle shots). Half the width, twice the depth: a sail under the spar.
        if (cloth) rig_sail_square(cloth, asset_find("bowsprit") ? NULL : rope, 0.60f, dy + 0.095f, dy + 0.030f, 0.085f);
    }
    // (the flag is no longer part of this mesh: it is a simulated cloth per ship, flag_push)
}

// Build the meshes for a class. `hb` is the hull's half beam and `ty[]` the mast tops, all in
// fractions of hull length measured from the waterline. The cordage is laid twice - new hemp
// and worn tarred rope (0.22.6) - and the ship picks one by her condition.
void Shipyard::rig_build(rig_cache_t *rc, const ship_class_t *c, int nm, const float *sx,
                      const float *ty, float dy, float hb, float bow, float stern, float castle) {
    mb_t rope = {0}, rope2 = {0}, cloth = {0};
    mb_material(&rope, 0.85f, 0.f);
    mb_material(&rope2, 0.85f, 0.f);
    mb_material(&cloth, 0.92f, 0.f);
    g_rope_col = rope_new;
    rig_build_cordage(&rope, &cloth, c, nm, sx, ty, dy, hb, bow, stern, castle);
    g_rope_col = rope_worn;
    rig_build_cordage(&rope2, NULL, c, nm, sx, ty, dy, hb, bow, stern, castle);
    g_rope_col = rope_col;
    rc->rope      = mesh_upload(&rope);
    rc->rope_worn = mesh_upload(&rope2);
    rc->cloth     = mesh_upload(&cloth);
    rc->built = 1;
}

const ship_class_t *Shipyard::by_name(const char *n) {
    classes_init();
    for (int i = 0; i < NCLASSES; ++i) if (!strcmp(g_classes[i].name, n)) return &g_classes[i];
    return NULL;
}

// Venice, the Ottomans and the pirates sail the oared interceptors the sheets call
// Mediterranean; the northern trading powers favour the broad-beamed hulk over the carrack.
int Shipyard::med_flavour(int nation)   { return nation == NATION_VENICE || nation == NATION_OTTOMAN || nation == NATION_PIRATES; }
int Shipyard::north_flavour(int nation) { return nation == NATION_ENGLAND || nation == NATION_NETHERLANDS || nation == NATION_DENMARK; }

// A game ship type is a role, not a hull: the same Frigate is an armed cog in the Channel and a
// galiot in the Adriatic.
const ship_class_t *Shipyard::class_for(int type, int nation) {
    switch (type) {
    case SHIP_SLOOP:    return med_flavour(nation) ? by_name("fusta") : by_name("coastal_trader");
    case SHIP_FLUYT:    return med_flavour(nation) ? by_name("galiot") : by_name("cog");
    case SHIP_MERCHANT: return north_flavour(nation) ? by_name("hulk") : by_name("carrack");
    case SHIP_FRIGATE:  return med_flavour(nation) ? by_name("galiot") : by_name("armed_cog");
    case SHIP_GALLEON:  return by_name("galleon");
    case SHIP_LINER:    return by_name("great_carrack");
    // 0.22.22 (issue18): the four new classes
    case SHIP_CARAVEL:  return med_flavour(nation) ? by_name("fusta") : by_name("caravel");
    case SHIP_HULK:     return by_name("hulk");
    case SHIP_XEBEC:    return by_name("xebec");
    case SHIP_WAR_GALLEON: return by_name("war_carrack");
    default:            return NULL;
    }
}

// New canvas or used (0.22.6, issue3). A ship's suit of sails and her cordage are either
// fresh from the arsenal or worn: the two sailcloth tiles and the two rope colours. It is a
// stable property of the hull - the same ship always looks the same - from her key, biased by
// who sails her: a pirate is in rags, a state navy mostly in new canvas.
int Shipyard::ship_quality(const shipyard_req_t *r) {
    const ship_class_t *c = class_for(r->type, r->nation);
    if (c && c->sails == 1) return 1;   // the class file says new canvas (0.22.11)
    if (c && c->sails == 2) return 0;   // ... or worn
    unsigned h = r->key * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    float u = (h & 0xFFFFu) / 65535.f;
    float newish = r->nation == NATION_PIRATES ? 0.15f : 0.62f;
    return u < newish;
}

const char *Shipyard::class_name(int type, int nation) {
    const ship_class_t *c = class_for(type, nation);
    return c ? c->name : "procedural";
}

int Shipyard::class_stats(int type, int nation, ship_stats_t *out) {
    const ship_class_t *c = class_for(type, nation);
    if (c) { *out = c->st; return 1; }
    // no authored class: numbers from the gameplay row, so callers never divide by zero
    const ship_info_t *t = &ship_types[type < 0 ? 0 : type >= SHIP_COUNT ? SHIP_COUNT - 1 : type];
    float len = ship_scale(type) / SHIP_M_PER_UNIT;
    *out = ship_stats_t{ len, len * 0.28f, len * 0.10f, len * len * 0.35f, t->speed * 5.f, 0.f, 120.f / len, t->guns, t->crew };
    return 0;
}

// The mast standing at station k: the per-type library part first (mast_lib, the authored
// assemblies from assets/ships/new_masts), else the class's old split part (`_m<part>`), else
// the single narrowed `_m0`, else the whole generated rig - a ship always gets *some* mast.
asset_t *Shipyard::mast_asset(const ship_class_t *c, int k) {
    if (c->mast_lib[k] && asset_find(c->mast_lib[k]))
        return asset_get_role(ASSET_SHIP_PART, c->mast_lib[k]);
    char role[80];
    if (c->split) { snprintf(role, sizeof role, "%s_m%d", c->masts, c->mast_part[k]); return asset_get_role(ASSET_SHIP_PART, role); }
    snprintf(role, sizeof role, "%s_m0", c->masts);
    asset_t *a = asset_find(role) ? asset_get_role(ASSET_SHIP_PART, role) : NULL;
    return a ? a : asset_get_role(ASSET_SHIP_PART, c->masts);
}

// The authored square yard for station k's layer (0 course, 1 topsail, 2 topgallant): the mast
// library's own split yard where the import has it, the generic square_yard otherwise. NULL
// while nothing is loaded - the sail then carries its procedural spar.
asset_t *Shipyard::yard_asset(const ship_class_t *c, int k, int layer) {
    const char *name = NULL;
    if (c->mast_lib[k])
        for (unsigned i = 0; i < sizeof yard_sets / sizeof yard_sets[0]; ++i)
            if (!strcmp(yard_sets[i].mast, c->mast_lib[k])) { name = yard_sets[i].yard[layer < 0 ? 0 : layer > 2 ? 2 : layer]; break; }
    asset_t *a = name && asset_find(name) ? asset_get_role(ASSET_SHIP_PART, name) : NULL;
    if (!a && asset_find("square_yard")) a = asset_get_role(ASSET_SHIP_PART, "square_yard");
    return a;
}

// ---------------------------------------------------------------------------
// Flags: a verlet cloth per ship (see flag_sim_t). Everything is in the ship's frame in
// fractions of hull length, +x bow, +y up, +z starboard; the mesh shader applies roll, pitch
// and the swell through the rig instance, so the flag never leaves its masthead.

flag_sim_t *Shipyard::flag_slot(unsigned key) {
    flag_sim_t *lru = &flags[0];
    for (int i = 0; i < FLAG_MAX; ++i) {
        if (flags[i].inited && flags[i].key == key) return &flags[i];
        if (!flags[i].inited) { lru = &flags[i]; break; }
        if (flags[i].last_seen < lru->last_seen) lru = &flags[i];
    }
    lru->key = key; lru->inited = 0;      // (re)initialised by flag_push on first use
    return lru;
}

void Shipyard::flag_step(flag_sim_t *f, float staff_x, float staff_y, float g_local, float wl_x, float wl_z, float dt) {
    const float dx = RIG_FLAG_LEN / (FLAG_W - 1), dy = RIG_FLAG_H / (FLAG_H - 1), diag = sqrtf(dx*dx + dy*dy);
    float t = RF.anim_time, ph = (float)(f->key & 255u) * 0.07f;   // the scene clock (0.22.7): a paused battle freezes the flags
    float wspd = sqrtf(wl_x*wl_x + wl_z*wl_z);
    for (int j = 0; j < FLAG_H; ++j) for (int i = 1; i < FLAG_W; ++i) {
        float *p = f->p[j][i], *pp = f->pp[j][i];
        float vx = (p[0] - pp[0]) / dt, vy = (p[1] - pp[1]) / dt, vz = (p[2] - pp[2]) / dt;
        // drag towards the air's own motion, gravity, and a little turbulence that grows with
        // the wind and runs down the fly so the cloth ripples rather than hangs
        float k = 6.0f, fl = (float)i / (FLAG_W - 1);
        float gust = 0.35f * wspd * (sinf(t * 9.0f + fl * 7.0f + ph) + 0.5f * sinf(t * 14.3f + fl * 11.0f + ph * 1.7f));
        float ax = k * (wl_x - vx), ay = k * (0.f - vy) * 0.5f - g_local + 0.25f * gust * wspd, az = k * (wl_z - vz) + gust;
        float nx = p[0] + (p[0] - pp[0]) * 0.985f + ax * dt * dt;
        float ny = p[1] + (p[1] - pp[1]) * 0.985f + ay * dt * dt;
        float nz = p[2] + (p[2] - pp[2]) * 0.985f + az * dt * dt;
        pp[0] = p[0]; pp[1] = p[1]; pp[2] = p[2]; p[0] = nx; p[1] = ny; p[2] = nz;
    }
    for (int j = 0; j < FLAG_H; ++j) {   // the hoist is bent to the staff
        float *p = f->p[j][0]; p[0] = staff_x; p[1] = staff_y + j * dy; p[2] = 0.f;
        f->pp[j][0][0] = p[0]; f->pp[j][0][1] = p[1]; f->pp[j][0][2] = p[2];
    }
    for (int it = 0; it < 4; ++it) {
        #define FLAG_LINK(a, b, rest, stiff) do { \
            float ddx = (b)[0]-(a)[0], ddy = (b)[1]-(a)[1], ddz = (b)[2]-(a)[2]; \
            float dl = sqrtf(ddx*ddx + ddy*ddy + ddz*ddz); if (dl > 1e-7f) { \
            float corr = ((dl - (rest)) / dl) * (stiff); float wa = pin_a ? 0.f : (pin_b ? 1.f : 0.5f), wb = pin_b ? 0.f : (pin_a ? 1.f : 0.5f); \
            (a)[0] += ddx * corr * wa; (a)[1] += ddy * corr * wa; (a)[2] += ddz * corr * wa; \
            (b)[0] -= ddx * corr * wb; (b)[1] -= ddy * corr * wb; (b)[2] -= ddz * corr * wb; } } while (0)
        for (int j = 0; j < FLAG_H; ++j) for (int i = 0; i < FLAG_W; ++i) {
            int pin_a = i == 0, pin_b;
            if (i + 1 < FLAG_W) { pin_b = 0; FLAG_LINK(f->p[j][i], f->p[j][i+1], dx, 1.0f); }
            if (j + 1 < FLAG_H) { pin_b = pin_a; FLAG_LINK(f->p[j][i], f->p[j+1][i], dy, 1.0f); }
            if (i + 1 < FLAG_W && j + 1 < FLAG_H) { pin_b = 0; FLAG_LINK(f->p[j][i], f->p[j+1][i+1], diag, 0.5f); }
            if (i + 1 < FLAG_W && j > 0)          { pin_b = 0; FLAG_LINK(f->p[j][i], f->p[j-1][i+1], diag, 0.5f); }
        }
        #undef FLAG_LINK
    }
}

void Shipyard::flag_build_mesh(flag_sim_t *f, float r, float g, float b) {
    // (FLAG_W-1)*(FLAG_H-1) quads, two flat triangles each, in the builder's 12-float layout
    enum { NV = (FLAG_W - 1) * (FLAG_H - 1) * 6 };
    float v[NV * MB_FLOATS]; int n = 0;
    // 0.22.17: the simulated grid is a rectangle; the *drawn* cloth tapers along the fly and is
    // forked at its end, so a square of cloth reads as a swallow-tailed streamer. Shape only -
    // the physics behind it is untouched.
    float d[FLAG_H][FLAG_W][3];
    const float mid = 0.5f * (FLAG_H - 1);
    for (int i = 0; i < FLAG_W; ++i) {
        float fl = (float)i / (FLAG_W - 1), taper = 1.f - (1.f - RIG_FLAG_TAPER) * fl * fl;
        float cx = 0, cy = 0, cz = 0;
        for (int j = 0; j < FLAG_H; ++j) { cx += f->p[j][i][0]; cy += f->p[j][i][1]; cz += f->p[j][i][2]; }
        cx /= FLAG_H; cy /= FLAG_H; cz /= FLAG_H;
        int ia = i > 0 ? i - 1 : i, ib = i + 1 < FLAG_W ? i + 1 : i;
        float fx2 = f->p[0][ib][0] - f->p[0][ia][0], fy2 = f->p[0][ib][1] - f->p[0][ia][1], fz2 = f->p[0][ib][2] - f->p[0][ia][2];
        float fln = sqrtf(fx2*fx2 + fy2*fy2 + fz2*fz2); if (fln > 1e-6f) { fx2 /= fln; fy2 /= fln; fz2 /= fln; }
        for (int j = 0; j < FLAG_H; ++j) {
            float notch = RIG_FLAG_FORK * RIG_FLAG_LEN * fl * fl * (1.f - fabsf(j - mid) / mid);
            d[j][i][0] = cx + (f->p[j][i][0] - cx) * taper - fx2 * notch;
            d[j][i][1] = cy + (f->p[j][i][1] - cy) * taper - fy2 * notch;
            d[j][i][2] = cz + (f->p[j][i][2] - cz) * taper - fz2 * notch;
        }
    }
    for (int j = 0; j + 1 < FLAG_H; ++j) for (int i = 0; i + 1 < FLAG_W; ++i) {
        const float *q[4] = { d[j][i], d[j][i+1], d[j+1][i+1], d[j+1][i] };
        const int tri[6] = { 0, 1, 2, 0, 2, 3 };
        float ux = q[1][0]-q[0][0], uy = q[1][1]-q[0][1], uz = q[1][2]-q[0][2];
        float wx = q[3][0]-q[0][0], wy = q[3][1]-q[0][1], wz = q[3][2]-q[0][2];
        float nx = uy*wz - uz*wy, ny = uz*wx - ux*wz, nz = ux*wy - uy*wx;
        float l = sqrtf(nx*nx + ny*ny + nz*nz); if (l > 1e-9f) { nx /= l; ny /= l; nz /= l; } else { nx = 0; ny = 0; nz = 1; }
        for (int k = 0; k < 6; ++k) {
            float *o = v + (n++) * MB_FLOATS; const float *p = q[tri[k]];
            o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = nx; o[4] = ny; o[5] = nz;
            o[6] = r; o[7] = g; o[8] = b; o[9] = 0.f; o[10] = 0.88f; o[11] = 0.f;
        }
    }
    if (!f->built) { f->mesh.geo = rhi_geometry_make(&mesh_layout, v, n, 0, 0); f->mesh.ninst = 0; f->built = 1; }
    else rhi_geometry_update(&f->mesh.geo, v, n);
}

// Called per rigged ship each frame: integrate, rebuild the mesh, remember the instance.
void Shipyard::flag_push(const shipyard_req_t *r, const ship_class_t *c, float staff_x, float staff_y,
                         float wl_x, float wl_z, const inst_t *ri) {
    flag_sim_t *f = flag_slot(r->key);
    const float dx = RIG_FLAG_LEN / (FLAG_W - 1), dy = RIG_FLAG_H / (FLAG_H - 1);
    if (!f->inited) {   // hang it straight downwind of the staff to start
        float wl = sqrtf(wl_x*wl_x + wl_z*wl_z), ux = wl > 1e-4f ? wl_x / wl : -1.f, uz = wl > 1e-4f ? wl_z / wl : 0.f;
        for (int j = 0; j < FLAG_H; ++j) for (int i = 0; i < FLAG_W; ++i) {
            float *p = f->p[j][i]; p[0] = staff_x + ux * i * dx; p[1] = staff_y + j * dy; p[2] = uz * i * dx;
            memcpy(f->pp[j][i], p, sizeof(float) * 3);
        }
        f->inited = 1; f->step_t = RF.anim_time;
    }
    float g_local = 9.81f / maxf(c->length_m, 5.f);    // metres per second squared, in hull fractions
    float dt = clampf(RF.anim_time - f->step_t, 0.f, 0.05f);
    if (dt > 0.f) {
        float h = dt / FLAG_SUBSTEPS;
        for (int k = 0; k < FLAG_SUBSTEPS; ++k) flag_step(f, staff_x, staff_y, g_local, wl_x, wl_z, h);
        f->step_t = RF.anim_time;
    }
    f->last_seen = RF.time;
    flag_build_mesh(f, r->cr, r->cg, r->cb);
    f->inst = *ri; f->drawn = 1;
    shipyard_flags_live++;
}

// 0.22.13 (issue10 "there are cannons, add them to ships"): which of the imported guns a class
// carries at her ports (assets/ships/cannons, descriptors gun_*), and how long it is in metres.
// The carracks mount the long cannon, the northern merchantmen the stone-throwing perrier,
// the galleys and the small traders swivel guns on the rail. ship_classes.json "gun"/"gun_m"
// override it per class.
static const char *gun_role_for(const ship_class_t *c) {
    if (c->gun && c->gun[0]) return c->gun;
    if (!c->name) return NULL;
    if (!strcmp(c->name, "war_carrack") || !strcmp(c->name, "great_carrack")) return "gun_cannon_large";
    if (!strcmp(c->name, "carrack"))     return "gun_cannon_late15c";
    if (!strcmp(c->name, "hulk") || !strcmp(c->name, "armed_cog")) return "gun_perrier";
    if (!strcmp(c->name, "galiot"))      return "gun_swivel_heavy";
    if (!strcmp(c->name, "fusta") || !strcmp(c->name, "cog") || !strcmp(c->name, "coastal_trader")) return "gun_swivel_light";
    return c->st.guns >= 16 ? "gun_cannon_large" : "gun_breech_light";
}
static float gun_metres_for(const ship_class_t *c, const char *role) {
    if (c->gun_m > 0.f) return c->gun_m;
    if (!role) return 2.4f;
    if (!strcmp(role, "gun_cannon_large")) return 3.2f;
    if (!strcmp(role, "gun_cannon_late15c")) return 2.8f;
    if (!strcmp(role, "gun_bombard")) return 3.4f;
    if (!strcmp(role, "gun_perrier")) return 2.4f;
    if (!strcmp(role, "gun_breech_light")) return 2.0f;
    if (!strcmp(role, "gun_swivel_heavy")) return 1.6f;
    return 1.3f;
}
#define GUN_MIN_PX 120.f     // below this the guns are a few pixels and the hull's own gunports carry them

void Shipyard::request_all(void) {
    classes_init();
    for (int i = 0; i < NCLASSES; ++i) {
        const ship_class_t *c = &g_classes[i];
        asset_t *a; char role[80];
        if ((a = asset_find(c->hull)))             asset_request(a);
        for (int k = 0; k < SHIP_MAX_MASTS; ++k)
            if (c->mast_lib[k] && (a = asset_find(c->mast_lib[k]))) asset_request(a);
        if (c->masts) {
            if ((a = asset_find(c->masts))) asset_request(a);
            for (int k = 0; k < 3; ++k) { snprintf(role, sizeof role, "%s_m%d", c->masts, k); if ((a = asset_find(role))) asset_request(a); }
        }
        if (c->oars  && (a = asset_find(c->oars)))  asset_request(a);
        { const char *g = gun_role_for(c); float gm; if (g && (a = asset_find(g))) asset_request(a); (void)gm; }
    }
    // the authored spars: bowsprit, lantina and the split square yards (the sails carry their
    // own procedural stand-in beyond YARD_ASSET_MIN_PX, where a decimated spar is a sliver)
    { asset_t *a; if ((a = asset_find("bowsprit"))) asset_request(a);
      if ((a = asset_find("lateen_yard"))) asset_request(a);
      if ((a = asset_find("square_yard"))) asset_request(a);
      for (unsigned i = 0; i < sizeof yard_sets / sizeof yard_sets[0]; ++i)
          for (int l = 0; l < 3; ++l) if ((a = asset_find(yard_sets[i].yard[l]))) asset_request(a); }
}

int Shipyard::push(const shipyard_req_t *r) {
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

    inst_t in = inst_t{ r->x, -water, r->z, r->heading, cscale,
                          r->cr, r->cg, r->cb, r->rock * resp, r->sel, resp, 0.f, r->dither };
    in.fx3 = clampf(r->hull_d, 0.f, 1.f);   // 0.22.23 (issue19): the shot scars, splinters and soot meshes.frag paints
    in.roll_b = r->roll; in.pitch_b = r->pitch;   // the body's attitude, inherited by every part below (0.22.12)
    scene_asset_instance(hull, in, r->key, r->camdist);
    shipyard_ships_drawn++;

    const int qual = ship_quality(r);       // 1 = new canvas and pale hemp, 0 = used (0.22.6)
    const float cloth_q = qual ? 2.f : 1.f;   // what meshes.vert reads as the sail's quality
    int nm = c->nmasts > 0 ? (c->nmasts > SHIP_MAX_MASTS ? SHIP_MAX_MASTS : c->nmasts) : 1;
    int main = 0;
    for (int k = 1; k < nm; ++k) if (c->mast_h[k] > c->mast_h[main]) main = k;
    #define MAST_OVERHANG 1.0f
    #define MAST_TRIM     0.85f
    // The generated poles are chunky for their height - at the battle zoom a cog's mast read as
    // a tree trunk - so they are thinned radially (gpu.h inst_t.slim), which a uniform scale
    // cannot do without losing the height the class table sets.
    #define MAST_FALL_SECS 2.6f      // 0.22.23 (issue19): how long a mast takes to go by the board
#define MAST_SLIM     0.64f      // 0.22.6: thicker again (issue3); 0.34 -> 0.425 -> 0.52; 0.22.12: 0.64 (issue9 "masts still not thick")
    // ... and stepped *into* the ship rather than stood on `deck`. That fraction is a per-class
    // guess at where the hull model's deck is, and where it guesses high the mast floated a
    // metre over the planking. A mast is stepped on the keelson in any case, so burying the heel
    // is both right and the safe direction to be wrong in: the deck hides it.
    #define MAST_STEP     0.20f      // 0.22.4: deeper (the cog's stood a metre over its deck)
    // The aft stations of a three- and four-master stand on the sterncastle, which the hull
    // models carry well above the `deck` fraction: those masts, their lantinas and canvas are
    // lifted by this share of the hull height so the sail's foot clears the poop (0.22.4).
    #define CASTLE_LIFT   0.14f      // 0.22.5: a little more ("sails too low")
    auto stn_lift = [&](int k) { return nm >= 3 && c->mast_at[k] <= -0.22f ? CASTLE_LIFT * hh : 0.f; };
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
                // 0.22.20 (issue16 "add mesh damage removal dynamically ... actually removing
                // parts of mesh"): a mast that has gone by the board is not drawn - not scaled,
                // not greyed, *gone*, with its yards and canvas below. The ship is assembled
                // from parts per frame, so removal really is removal. Stations fall from the
                // bow aft, matching the physics' masts_down count; the standing rigging is one
                // per-class mesh and still shows its old runs - cutting it per station needs
                // the per-ship rig buffer the verlet rope needs too (todo 121b).
                // 0.22.23 (issue19 "add animation of ... mast falling"): a mast that has been
                // carried away is drawn *going* for MAST_FALL_SECS - it heels over the side,
                // accelerating as a falling spar does, drops as its heel tears out of the
                // step, and is gone when it is in the water. Before this it simply ceased to
                // exist between one frame and the next, which is the thing the issue is
                // about. `fall` is 0 at the moment it is struck and 1 when it is overboard.
                float fall = -1.f;
                if (k < r->masts_down) {
                    float ft = k < SHIP_MAX_MASTS ? r->mast_fall[k] : -1.f;
                    if (ft < 0.f || ft > MAST_FALL_SECS) continue;      // long gone
                    fall = ft / MAST_FALL_SECS;
                }
                float dx = c->mast_at[k] * len;
                inst_t mi = in;
                mi.x = r->x + fx * dx; mi.z = r->z + fz * dx;
                mi.y = deck_y - MAST_STEP * hh + stn_lift(k);
                if (fall >= 0.f) {
                    // over the side, quickening (t^2), and down into the sea as she goes
                    float a = fall * fall;
                    mi.fx2 = a * 1.75f;                                  // the lathe pitch meshes.vert reads on a rigid part
                    mi.y -= a * 0.55f * hh;
                    mi.x += fx * a * 0.10f * len; mi.z += fz * a * 0.10f * len;   // and she slides forward off her step
                }
                // 0.22.8 (issue5 "fix size of rigging and masts"): a lesser mast is a shorter
                // *copy* of the same model, so scaling it to 0.8 of the main's height also made
                // it 0.8 as thick - the fore and mizzen read as sticks beside the main. The
                // radial slim is divided by the height ratio, so every mast on the ship has the
                // main's girth at the deck (a real foremast is nearly as stout as the main).
                float hk = c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f, hmain = c->mast_h[main] > 0.f ? c->mast_h[main] : 1.f;
                mi.slim = MAST_SLIM * clampf(hmain / hk, 1.f, 1.6f);
                mi.ax = r->x; mi.az = r->z;   // a mast is a fixed part of its ship: same swell, same roll (i_anchor)
                // Every mast part is fitted to the class's rig height, so the fore/main/mizzen
                // proportion comes from the class table either way - which keeps it a number
                // you can tune without re-exporting anything.
                mi.scale = ms * hk;
                scene_asset_instance(mp[k], mi, r->key ^ (unsigned)(0x5BD1u + k * 0x97u), r->camdist);
            }
        }
    }

    // Rigging and canvas: one instance each of the class's procedural rope and cloth meshes,
    // built the first time both the hull and the rig are resident (their sizes set the mast
    // stations). Skipped once the ship is small enough that cordage is below a pixel.
    // 0.22.15 (issue12 "don't hide sails with distance in battle view"): the staged scenes (scale > 1) keep every rig and sail at any distance
    if (r->scale > 1.5f || lod_px(len * 0.5f, r->camdist) > RIG_MIN_PX) {
        rig_cache_t *rc = &rig_cache[(int)(c - g_classes)];
        // 0.22.13 (issue10 "rigging to the top of the mast, not above"): the rope tops used the
        // *main* mast's model height for every station, measured from the deck, while the masts
        // themselves are each their own model stepped MAST_STEP below it (and lifted on the
        // castles) - so every shroud and stay ended a fifth of a hull height over its masthead,
        // and on a mizzen two thirds of the pole over it. Each station is measured from its own
        // asset now, from the heel, and the mesh is only built once every mast's bounds are in.
        int masts_ready = mref != NULL;
        for (int k = 0; k < nm && masts_ready; ++k) { asset_t *mk = mast_asset(c, k); if (!mk || mk->state != ASSET_LOADED || mk->bmax[1] - mk->bmin[1] < 0.05f) masts_ready = 0; }
        if (!rc->built && masts_ready) {
            float ml = (mref->bmax[0] - mref->bmin[0]) * cscale, ms = MAST_TRIM;
            if (ml > len * MAST_OVERHANG) ms = MAST_TRIM * len * MAST_OVERHANG / ml;
            float mh = (mref->bmax[1] - mref->bmin[1]) * cscale * ms;   // fitted rig height of the main, world units (the log line)
            float sxn[SHIP_MAX_MASTS], tyn[SHIP_MAX_MASTS];
            float dy = (hh * c->deck - water) / len;              // deck above the waterline
            for (int k = 0; k < nm; ++k) {
                asset_t *mk = mast_asset(c, k);
                float mhk = (mk->bmax[1] - mk->bmin[1]) * cscale * ms * (c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f);   // this station's pole, heel to truck
                sxn[k] = c->mast_at[k];
                tyn[k] = dy + (mhk - MAST_STEP * hh + stn_lift(k)) / len;   // the truck: the heel is MAST_STEP under the deck
            }
            // half beam from the hull model, but never wider than the class's own beam: a
            // scan with an oar bank or a wide rail would otherwise anchor the shrouds in the sea
            float hb = minf((hull->bmax[2] - hull->bmin[2]) * cscale * 0.5f / len, 0.5f * c->st.beam_m / c->length_m * 1.05f);
            float bowf = hull->bmax[0] * cscale / len * 0.96f, sternf = hull->bmin[0] * cscale / len * 0.94f;   // the hull's own ends (0.22.14)
            rig_build(rc, c, nm, sxn, tyn, dy, hb, bowf, sternf, CASTLE_LIFT * hh / len);
            LOG_DEBUG("gfx", "rigging built for '%s': %d masts, deck %.3f, rig %.3f of hull length",
                      c->name, nm, dy, mh / len);
        }
        if (rc->built) {
            float wdx, wdz, wstr; wind_at(r->x, r->z, G.day, &wdx, &wdz, &wstr);
            inst_t ri = in;
            ri.y = 0.f;                       // the rig mesh is modelled from the waterline
            ri.scale = len;                   // and in fractions of hull length
            ri.fx1 = cloth_q; ri.fx2 = wdx * wstr; ri.fx3 = wdz * wstr;
            // 0.22.23 (todo 121b): solve this ship's cordage forward and hand the shader the
            // chain's state instead of a canned sine. The wind is wanted *across* her deck,
            // which is what actually blows a bight sideways.
            {
                static double rig_prev_t = 0.0; static float rig_dt = 0.f;
                if (RF.anim_time != rig_prev_t) { rig_dt = (float)(RF.anim_time - rig_prev_t); rig_prev_t = RF.anim_time; }
                float across = -fz * (wdx * wstr) + fx * (wdz * wstr);   // starboard component
                float sw = 0.f, taut = 0.f;
                rig_sim_step(r->key, r->roll, across, rig_dt, &sw, &taut);
                ri.rig_swing = sw; ri.rig_taut = taut;
            }   // 0.22.8: the rope pipeline reads the quality for its texture (meshes/rope.geom); 0.22.15: and the wind, for the sag's swing
            // 0.22.23 (issue19 "rigging falling"): when every mast is over the side there is
            // nothing left for the standing rigging to be made fast to, so the whole rig goes
            // with the last of them. The shrouds and stays are one per-class mesh and cannot
            // be cut station by station - that still wants per-ship rope geometry - but a bare
            // hulk trailing a full suit of rigging was the worst of it, and it is gone.
            if (r->masts_down < nm)
            *instlist_push(qual ? &rig_inst[(int)(c - g_classes)] : &rig_worn_inst[(int)(c - g_classes)]) = ri;
            inst_t ci = ri; ci.fx1 = cloth_q;   // cloth: the wind model in meshes.c, and the suit of sails (ci inherits the solved cordage state)
            ci.fx2 = wdx * wstr; ci.fx3 = wdz * wstr;
            *instlist_push(&cloth_inst[(int)(c - g_classes)]) = ci;
            shipyard_rigged++;

            // --- the flag: a simulated cloth at the main masthead, in the ship's frame. The
            // apparent wind is the true wind turned into that frame less the ship's own way.
            // 0.22.21: no masthead, no flag - a felled mainmast used to leave its colours
            // hovering over the wreck of itself.
            if (main >= r->masts_down) {
                float ml = (mref ? (mref->bmax[0] - mref->bmin[0]) * cscale : len), msf = MAST_TRIM;
                if (ml > len * MAST_OVERHANG) msf = MAST_TRIM * len * MAST_OVERHANG / ml;
                float mh_main = mref ? (mref->bmax[1] - mref->bmin[1]) * cscale * msf * (c->mast_h[main] > 0.f ? c->mast_h[main] : 1.f) - MAST_STEP * hh + stn_lift(main) : 0.45f * len;   // 0.22.13: the truck as it stands, from the heel
                float dyf = (hh * c->deck - water) / len;
                float staff_x = c->mast_at[main] - 0.004f, staff_y = dyf + mh_main / len + 0.006f;
                float c_ = cosf(r->heading), s_ = sinf(r->heading);
                float wwx = wdx * wstr * FLAG_WIND_MS, wwz = wdz * wstr * FLAG_WIND_MS;             // metres per second, world
                float way = clampf(r->rock, 0.f, 1.2f) * FLAG_WAY_MS;
                float wl_x = (wwx * c_ - wwz * s_ - way) / c->length_m, wl_z = (wwx * s_ + wwz * c_) / c->length_m;   // ship frame, hull fractions per second
                flag_push(r, c, staff_x, staff_y, wl_x, wl_z, &ri);
            }

            // --- the yards. Every sail hangs from an *authored* spar when the ship is close
            // enough for the model to read - the lantina (assets/elm/lateen_yard) on the lateen
            // stations, the split square yards on the rest - instanced on the swung or braced
            // heading with the canvas, so yard and sail turn to the wind as one piece about the
            // mast. Past YARD_ASSET_MIN_PX the canvas mesh with its own spar takes over.
            float deck_y2 = -water + hh * c->deck;
            float msl = cscale * MAST_TRIM;
            if (mref) { float ml2 = (mref->bmax[0] - mref->bmin[0]) * cscale;
                        if (ml2 > len * MAST_OVERHANG) msl = cscale * MAST_TRIM * len * MAST_OVERHANG / ml2; }
            float wang = atan2f(-wdz, wdx);
            float rel = wrap_angle(wang - r->heading);
            // the lantina trims hard: up to ~70 degrees off the centreline toward the wind,
            // working a little harder the harder it blows
            // 0.22.17 (issue13/14 "sails should turn to get maximum efficiency from the wind"):
            // the bisector rule - a yard draws most when it halves the angle between the wind
            // and the keel: square running, braced sharp close-hauled.
            float trim = rel * 0.5f;
            float swing = clampf(trim, -1.30f, 1.30f)
                        + (0.03f + 0.05f * wstr) * sinf(RF.anim_time * (0.8f + 0.3f * wstr) + (float)(r->key & 31u) * 0.21f);
            // square yards brace less than a lantina trims: toward the wind, clamped, with the
            // same working oscillation on the ship's own phase
            float brace = clampf(trim, -0.95f, 0.95f)
                        + (0.02f + 0.04f * wstr) * sinf(RF.anim_time * (0.7f + 0.3f * wstr) + (float)(r->key & 31u) * 0.17f);
            if (!sq_cloth_built) {
                for (int q = 0; q < SQ_KINDS; ++q) for (int v = 0; v < 2; ++v) {
                    mb_t cl = mb_t{}; mb_material(&cl, 0.92f, 0.f);
                    rig_sail_square(&cl, v ? &cl : NULL, 0.f, 0.f, -sq_drop[q], 1.f);   // [1] carries the far spar
                    sq_cloth_mesh[q][v] = mesh_upload(&cl);
                }
                sq_cloth_built = 1;
            }
            if (!lateen_cloth_built) {
                for (int v = 0; v < 2; ++v) {
                    mb_t cl = mb_t{}; mb_material(&cl, 0.92f, 0.f);
                    rig_sail_lateen(&cl, v ? &cl : NULL, 0.f, 0.f, 1.f);
                    lateen_cloth_mesh[v] = mesh_upload(&cl);
                }
                lateen_cloth_built = 1;
            }
            for (int k = 0; k < nm; ++k) {
                asset_t *mk = mast_asset(c, k);
                if (!mk) continue;
                // 0.22.23 (issue19 "sails snapping ... and rigging falling"): canvas goes with
                // its mast, and goes *over the side with it* - drawn through the same fall,
                // collapsing as the spar swings down, instead of blinking out.
                float cfall = -1.f;
                if (k < r->masts_down) {
                    float ft = k < SHIP_MAX_MASTS ? r->mast_fall[k] : -1.f;
                    if (ft < 0.f || ft > MAST_FALL_SECS * 0.8f) continue;   // the canvas is in the water before the spar
                    cfall = ft / (MAST_FALL_SECS * 0.8f);
                }
                float mh_full = (mk->bmax[1] - mk->bmin[1]) * msl * (c->mast_h[k] > 0.f ? c->mast_h[k] : 1.f);   // heel to truck
                // 0.22.13 (issue10): the canvas and the spars are laid out in fractions of the mast
                // that *stands above the deck* - the heel is MAST_STEP under it - so a layer at 0.96
                // or a lateen peak at 0.98 is under the truck, where before both stood over it
                float mh = maxf(mh_full - MAST_STEP * hh, 0.3f * mh_full);
                float dxk = c->mast_at[k] * len;
                float bx = r->x + fx * dxk, bz = r->z + fz * dxk;   // the mast axis
                if (c->sail[k] == SAIL_LATEEN) {
                    float sw = r->heading + swing;
                    float sfx = cosf(sw), sfz = -sinf(sw);            // forward on the swung heading
                    // The lantina is the authored asset again (0.22.3, assets/elm/lateen_yard - the
                    // spar as it was taken from the import): scaled to the design line LAT_TACK ->
                    // LAT_PEAK and slung at its middle on the swung heading, the canvas cut [0]
                    // hanging under it. 0.22 had replaced it with the procedural spar because the
                    // scan's far LODs decimate to a sliver; the procedural one now only stands in
                    // past YARD_ASSET_MIN_PX, where the authored spar would be under a pixel wide.
                    float spar_len = (LAT_PEAK_Y - LAT_TACK_Y) * mh * 1.41421f;
                    asset_t *la = asset_find("lateen_yard") ? asset_get_role(ASSET_SHIP_PART, "lateen_yard") : NULL;
                    // 0.22.8: only a *loaded* asset with real bounds is placed - a part still
                    // streaming has zero bounds, and a scale computed from them is the giant
                    // spar of issue5 (two lantinas the size of the fleet lying across the sea)
                    if (la && (la->state != ASSET_LOADED || la->bmax[1] - la->bmin[1] < 0.05f)) la = NULL;
                    static int lat_authored = -1; if (lat_authored < 0) { const char *e = getenv("MARE_LATEEN_ASSET"); lat_authored = e && atoi(e) ? 1 : 0; }   // 0.22.14: the scanned lantina never sat on the head (issue5, 9, 10, 11); the sail carries its own spar now
                    int authored = lat_authored && la && lod_px(spar_len * 0.5f, r->camdist) >= YARD_ASSET_MIN_PX;
                    inst_t li = in;
                    li.heading = sw; li.x = bx; li.z = bz; li.y = deck_y2 + stn_lift(k); li.scale = mh;
                    li.fx1 = cloth_q; li.fx2 = wdx * wstr; li.fx3 = wdz * wstr;
                    li.ax = r->x; li.az = r->z;
                    if (cfall >= 0.f) { float a = cfall * cfall; li.y -= a * 0.75f * hh; li.scale *= 1.f - 0.55f * a; }   // 0.22.23: down with her spar
                    if (r->furl < 0.5f && r->rig_d < 0.45f) *instlist_push(&lateen_cloth_inst[authored ? 0 : 1]) = li;   // 0.22.15: furled = no canvas; 0.22.21: a half-ruined rig has lost her lateens
                    if (authored) {
                        // the asset's rise is its y extent; scale it to the design line, put its
                        // middle on the line's middle (the model's pivot is its slung point)
                        // 0.22.13 (issue10 "the front lateen as wide as the sail and connected to
                        // it"): the spar was scaled by its *rise* alone (and then a tenth more), so a
                        // scan raked differently from the design line came out longer or shorter
                        // than the head it carries, and off it at one end. It is scaled by its true
                        // length along its own axis to the design line's length, centred on the
                        // line's middle, and turned in its own plane (inst_t.fx2, the oars' pitch)
                        // by the difference in rake - so both ends sit on the head's ends, every
                        // roband is a real lashing, and the canvas is bent to it along its whole length.
                        float xext = maxf(la->bmax[0] - la->bmin[0], 1e-3f), yext = maxf(la->bmax[1] - la->bmin[1], 1e-3f);
                        float dline_x = (LAT_TACK_X - LAT_PEAK_X) * mh, dline_y = (LAT_PEAK_Y - LAT_TACK_Y) * mh;
                        float design_len = sqrtf(dline_x * dline_x + dline_y * dline_y), asset_len = sqrtf(xext * xext + yext * yext);
                        float s = design_len / asset_len;
                        if (design_len * 1.1f > len * 1.6f) {   // never a spar longer than the hull and a half: log it instead of drawing it
                            static int warned; if (!warned) { warned = 1; LOG_WARN("gfx", "lateen_yard scale %.2f (extent %.3f x %.3f, mh %.2f, len %.2f) implausible: spar suppressed", s, xext, yext, mh, len); }
                            continue;
                        }
                        // the model's rake (forward is -x: the tack end is +x, the peak -x and up)
                        float rake_asset = atan2f(yext, xext), rake_design = atan2f(dline_y, dline_x);
                        float turn = rake_asset - rake_design;
                        float acx0 = (la->bmin[0] + la->bmax[0]) * 0.5f * s, acy0 = (la->bmin[1] + la->bmax[1]) * 0.5f * s;
                        float acx = acx0 * cosf(turn) - acy0 * sinf(turn), acy = acx0 * sinf(turn) + acy0 * cosf(turn);   // the box's middle after the turn about the pivot
                        float cx = 0.5f * (LAT_TACK_X + LAT_PEAK_X) * mh - acx, cy = 0.5f * (LAT_TACK_Y + LAT_PEAK_Y) * mh - acy;
                        inst_t yi = in;
                        yi.heading = sw; yi.scale = s;
                        yi.x = bx + sfx * cx; yi.z = bz + sfz * cx; yi.y = deck_y2 + cy + stn_lift(k);
                        yi.fx2 = turn;   // turned about its own middle to the design rake (meshes.vert rigid pitch); the sign follows -x forward: a steeper model is eased down
                        yi.ax = r->x; yi.az = r->z;
                        yi.r = 1.f; yi.g = 1.f; yi.b = 1.f;
                        scene_asset_instance(la, yi, r->key ^ (unsigned)(0x71A3u + k * 0x53u), r->camdist);
                        shipyard_yards_authored++;
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
                    float top_w = deck_y2 + mh * ly->l[l].top + stn_lift(k);
                    float hw_w  = mh * ly->l[l].hw;
                    float yard_len = 2.f * hw_w * 1.06f;
                    asset_t *ya = yard_asset(c, k, l);
                    if (ya && (ya->state != ASSET_LOADED || ya->bmax[2] - ya->bmin[2] < 0.05f)) ya = NULL;   // 0.22.8: see the lantina above
                    int authored = ya && lod_px(yard_len * 0.5f, r->camdist) >= YARD_ASSET_MIN_PX;
                    inst_t qi = in;
                    qi.heading = bhd;
                    qi.x = bx + cb * off; qi.z = bz + sb * off;
                    qi.y = top_w; qi.scale = hw_w;
                    qi.fx1 = cloth_q; qi.fx2 = wdx * wstr; qi.fx3 = wdz * wstr;
                    qi.ax = r->x; qi.az = r->z;
                    // 0.22.21 (issue17 "add vidible damage on ships when fighting. sails can
                    // burn also"): a wounded rig loses her canvas from the top down - the
                    // topgallants blow out or burn first, the topsails next, the courses last,
                    // each before the mast itself goes by the board. The physics already eats
                    // d_rig when she burns, so a fire strips her sails on the way to felling
                    // her masts, which is the visible arc of a ship lost to fire.
                    int sail_gone = r->rig_d > 0.70f - 0.25f * (float)(l > 2 ? 2 : l);   // topgallant at 0.20, topsail 0.45, course 0.70
                    if (cfall >= 0.f) { float a = cfall * cfall; qi.y -= a * 0.80f * hh; qi.scale *= 1.f - 0.60f * a; }   // 0.22.23: the canvas collapses as the mast swings down
                    if (r->furl < 0.5f && !sail_gone) *instlist_push(&sq_cloth_inst[(int)ly->l[l].kind][authored ? 0 : 1]) = qi;
                    if (authored) {
                        // the yard lies across the ship (descriptor forward +z): its z extent is
                        // the spar; scale that to the sail's head and centre it on the mast line
                        float zext = maxf(ya->bmax[2] - ya->bmin[2], 1e-3f);
                        float s = yard_len / zext;
                        if (yard_len > len * 1.3f || (ya->bmax[0] - ya->bmin[0]) * s > len) {   // a yard wider than the ship, or a wrong-node asset standing tall: not drawn
                            static int warned; if (!warned) { warned = 1; LOG_WARN("gfx", "'%s' yard scale %.2f (extents %.3f x %.3f x %.3f, len %.2f) implausible: spar suppressed", ya->desc.name, s, ya->bmax[0] - ya->bmin[0], ya->bmax[1] - ya->bmin[1], zext, len); }
                            continue;
                        }
                        float acx = (ya->bmin[0] + ya->bmax[0]) * 0.5f * s, acy = (ya->bmin[1] + ya->bmax[1]) * 0.5f * s;
                        inst_t yi = in;
                        yi.heading = bhd; yi.scale = s;
                        yi.x = bx + cb * (off - acx); yi.z = bz + sb * (off - acx); yi.y = top_w - acy;
                        yi.ax = r->x; yi.az = r->z;
                        yi.r = 1.f; yi.g = 1.f; yi.b = 1.f;
                        scene_asset_instance(ya, yi, r->key ^ (unsigned)(0x2C9Bu + k * 0x61u + l * 0x1Fu), r->camdist);
                        shipyard_yards_authored++;
                    }
                }
            }
            // --- the authored bowsprit, where the class carries one: stands where the
            // procedural spar stood (rig_build draws only the spritsail canvas then)
            if (c->spritsail && asset_find("bowsprit")) {
                asset_t *bs = asset_get_role(ASSET_SHIP_PART, "bowsprit");
                if (bs && bs->state == ASSET_LOADED && bs->bmax[0] - bs->bmin[0] > 0.05f) {   // 0.22.8: loaded, with bounds
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

    // The guns (0.22.13, issue10): one authored gun at every port on both sides, on the same
    // stations the broadside's flashes come from (gfx/particles.cc broadside: half the class's
    // guns a side along the middle two thirds of the hull), pointing outboard, their carriages
    // on the gun deck a little under the weather deck - the swivel guns of the small craft on
    // the rail itself. Rigid to the hull like the masts (anchor, roll, pitch). Only while the
    // ship is large on screen; the hull's own painted ports carry them further off.
    // 0.22.14 (issue11 round): off by default - the hull scans carry their guns painted and the
    // authored parts doubled them; MARE_GUNS=1 stands them again until the hulls are re-made bare
    static int guns_on = -1; if (guns_on < 0) { const char *e = getenv("MARE_GUNS"); guns_on = e && atoi(e) ? 1 : 0; }
    if (guns_on && lod_px(len * 0.5f, r->camdist) > GUN_MIN_PX) {
        const char *grole = gun_role_for(c);
        asset_t *gun = grole && asset_find(grole) ? asset_get_role(ASSET_SHIP_PART, grole) : NULL;
        if (gun && gun->state == ASSET_LOADED && gun->bmax[0] - gun->bmin[0] > 0.05f) {
            int swivel = strstr(grole, "swivel") != NULL;
            int nside = (int)clampf((float)c->st.guns * 0.5f, 1.f, 18.f);
            float gm = gun_metres_for(c, grole);
            float gscale = gm / maxf(c->length_m, 5.f) * len / maxf(gun->bmax[0] - gun->bmin[0], 0.05f);   // the asset is a unit length along the barrel
            float hbw = minf((hull->bmax[2] - hull->bmin[2]) * cscale * 0.5f, 0.5f * c->st.beam_m / c->length_m * len * 1.05f);   // half beam, world units
            // MARE_GUN_OUT / MARE_GUN_UP / MARE_GUN_SCALE: tuning knobs for a screenshot run (multipliers)
            static float k_out = -1.f, k_up = 1.f, k_sc = 1.f;
            if (k_out < 0.f) { k_out = 1.f; const char *e; if ((e = getenv("MARE_GUN_OUT"))) k_out = (float)atof(e); if ((e = getenv("MARE_GUN_UP"))) k_up = (float)atof(e); if ((e = getenv("MARE_GUN_SCALE"))) k_sc = (float)atof(e); }
            gscale *= k_sc;
            float out = (swivel ? hbw * 1.00f : hbw * 0.90f) * k_out;   // a carriage gun stands with its muzzle through the port (the hull models' bulwarks are thick); a swivel on the rail
            float gy = -water + hh * c->deck * (swivel ? 1.04f : 0.97f) * k_up;
            float half = 0.34f * len;
            for (int side = -1; side <= 1; side += 2) {
                for (int g = 0; g < nside; ++g) {
                    float along = nside > 1 ? -half + 2.f * half * (float)g / (float)(nside - 1) : 0.f;
                    inst_t gi = in;
                    gi.x = r->x + fx * along + sx * out * side; gi.z = r->z + fz * along + sz * out * side; gi.y = gy;
                    gi.heading = r->heading - side * 1.5708f;    // the barrel outboard (heading 0 = +x forward; starboard is heading - 90 degrees)
                    gi.scale = gscale; gi.slim = 0.f; gi.sel = 0.f;
                    gi.r = 1.f; gi.g = 1.f; gi.b = 1.f;
                    gi.ax = r->x; gi.az = r->z;
                    scene_asset_instance(gun, gi, r->key ^ (unsigned)(0x6D2Bu + g * 2 + (side > 0)), r->camdist);
                }
            }
            shipyard_guns_drawn += 2 * nside;
        }
    }

    // Oar bank: one instance per oar, both sides, swept a little aft and dipped outboard. Only
    // while the ship is large on screen - an oar is a twentieth of the hull.
    static float oar_min_px = -1.f;
    if (oar_min_px < 0.f) { const char *e = getenv("MARE_OAR_MIN_PX"); oar_min_px = e ? (float)atof(e) : (float)OAR_MIN_PX; }
    if (c->oars && c->oar_pairs > 0 && lod_px(len * 0.5f, r->camdist) > oar_min_px) {
        asset_t *oar = asset_get_role(ASSET_SHIP_PART, c->oars);
        if (oar) {
            float span = c->oar_span * len, half = c->beam_frac * len;
            // 0.22.21 (issue17, the oar "wings"): the oar was drawn at the HULL's import scale.
            // The oar GLB is normalised like every Meshy asset - it spans ~2 units, the same as
            // a whole hull - so at `cscale` each sweep came out as long as the ship: a numeric
            // check printed the blade 12.5 units outboard of a 1.25-unit rail, and the bank
            // stood over the deck like wings, which is the issue's screenshot. A fusta's sweep
            // is ~0.38 of her length tip to tip; the instance is scaled to that, from the
            // model's own measured axis length (2.583 units tip to tip, on a -45-degree
            // diagonal, blade at the +x,-y end - see 0.22.20).
            // 0.22.25 - and this is the whole story of four rounds of wrong oars.
            //
            // 0.22.21 sized the sweep from the *raw GLB*, which spans 2.583 units tip to tip
            // on a -45-degree diagonal. But the asset loader **re-fits every part**: the log
            // says `loaded 'galley_oar' ... size 0.26x0.02x0.01, scale 0.1006`. The loaded
            // model is a thin stick 0.26 long on its own x - not 2.583, and not diagonal. So
            // `oscale = 0.38 * len / 2.583` rendered each oar at 0.26 * 1.67 = **0.43 world
            // units on an eleven-unit hull** - a sliver, which is why the bank simply was not
            // there. Worse, the numeric probe measured tips at 0.913 * scale, the raw model's
            // half-extent, so it reported a handsome 4.2-unit sweep that was never drawn:
            // every "verified" number since 0.22.21 was self-consistent and fictional.
            //
            // The sizing that was right all along is the one from before any of that: take
            // the asset's **own loaded extent** at the hull's import scale.
            float olen = (oar->bmax[0] - oar->bmin[0]) * cscale;   // tip to tip, world units
            float out = half * 0.96f + olen * OAR_INBOARD;         // centre of the oar, just outboard of the rail
            (void)out;
            float y = -water + hh * c->deck * 1.02f;        // 0.22.6: the ports are just under the rail, not half-way down the side (issue3: "oars are too low in water")
            float pull = clampf(r->rock, 0.f, 1.6f);                      // how hard she is working
            // A sweep is pulled at 20-26 strokes a minute, not the fast churn this used to run:
            // rate ~2.2 rad/s flat out. The blade dips on the pull and lifts on the recovery -
            // the instance transform has only yaw and height, and height is exactly the part of
            // the cycle that reads from the quarterdeck.
            // 0.22.17 (issue14 "oars again wrong: wide part needs to be in water, pivot in the
            // ship part, movement slowly 30 deg, -15 to 15"): the sweep is fifteen degrees either
            // side of the square and slow - a stroke every five seconds at rest, under three
            // pulling hard, which is the 20-26 a minute a sweep is actually pulled at.
            float rate = 1.20f + 0.95f * pull, reach = 0.2618f;   // 15 degrees
            // 0.22.21: the mount, adjustable at runtime while the oars are argued over -
            // MARE_OAR_SIDE flips which way the bank is hung, MARE_OAR_PITCH pins the blade
            // angle (radians; the default is what the measured -45-degree model wants)
            static float oar_side = 0.f, oar_pitch = -1.f;
            if (oar_side == 0.f) { const char *e = getenv("MARE_OAR_SIDE"); oar_side = e && atof(e) < 0 ? -1.f : 1.f;
                                   const char *q = getenv("MARE_OAR_PITCH"); if (q) oar_pitch = (float)atof(q); }
            // 0.22.23b: **reverted to the 0.22.21 stroke.** The drive-and-recovery cycle added
            // in 0.22.23 was asked for and was right in principle, but it moved the blade's
            // pitch as well as its swing - it feathered the oar by *dropping fx2*, and fx2 is
            // not only the blade angle: the rowlock mount below solves the oar's placement
            // from it, so the whole bank shifted and lifted as the stroke ran, and the look
            // that had been verified numerically in 0.22.21 was lost. The sine below is the
            // state the oars were in when they were right. A rowing cycle can come back, but
            // it has to drive the *swing* only and leave the mount's pitch alone.
            float oph = RF.anim_time * rate + (float)(r->key & 63u) * 0.09f;
            float stroke = reach * sinf(oph);
            float dip = 0.004f * len * clampf(cosf(oph), 0.f, 1.f) * (0.3f + 0.7f * pull);
            const float side_sign = 1.f;
            for (int i = 0; i < c->oar_pairs; ++i) {
                float t = c->oar_pairs > 1 ? i / (float)(c->oar_pairs - 1) - 0.5f : 0.f;
                float along = t * span;
                for (int side = -1; side <= 1; side += 2) {
                    inst_t oi = in;
                    // 0.22.17 (issue14 "pivot in the ship part"): the oar turns about its
                    // *rowlock*, a point on the rail, not about its own middle hung outboard.
                    // The model is centred on its length, so the instance is placed at the
                    // rowlock less the rotated model-space offset of that point - the same
                    // trick the lantina uses - and the loom then swings inboard over the deck
                    // while the blade sweeps outboard, as it does in a boat.
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
                    // 0.22.11 (issue8 "oars wrong side"): with heading - 90 the model's +x lay
                    // along the ship's own side direction, so the blade (-x) pointed *inboard*
                    // and the whole bank lay across the deck. Plus 90 puts the blade outboard.
                    // 0.22.20 (issue16, the *third* report of "wide part needs to be in water
                    // and thin part in ship" - this time the asset was measured instead of
                    // assumed). The GLB says: the oar is modelled on a **diagonal**, its long
                    // axis at -45 degrees in its own x-y plane, blade toward (+x, -y), loom
                    // toward (-x, +y); the blade end's cross-section is 0.23 units against the
                    // loom's 0.09. Every earlier fix believed the blade was the -x end, so the
                    // mount put +x - the real blade - *inboard*: the wide part buried in the
                    // hull, the thin loom sticking outboard and upward. Exactly what the
                    // screenshots show. The side sign is therefore flipped (blade outboard),
                    // and with the model's own -45 droop the fx2 pitch below brings the blade
                    // to ~20 degrees under the horizontal and the loom level over the deck.
                    oi.heading = r->heading + oar_side * side * (1.5708f + 0.17f - stroke * side_sign);   // 0.22.25: the sign the oars had before I ever called them fixed
                    // the sweep angles *down* into the water (0.22.5): a pitch about the oar's own
                    // z axis (meshes.vert reads fx2 on rigid instances), steeper on the pull
                    // 0.22.6: a steeper blade angle from the higher port, so the loom rises over
                    // the gunwale and only the blade is in the water
                    // The blade is the model's -x end (measured: its z extent is twice the
                    // loom's), and a positive pitch about the model's z carries -x downward -
                    // so this is the sign that puts the wide part in the water.
                    oi.fx2 = (oar_pitch >= 0.f ? oar_pitch : 0.46f) + 0.14f * clampf(cosf(oph), 0.f, 1.f) * (0.3f + 0.7f * pull);
                    {   // the rowlock on the rail, and the oar hung from it (the pre-claim mount)
                        float rlx = r->x + fx * along + sx * (half * 0.99f) * side;
                        float rlz = r->z + fz * along + sz * (half * 0.99f) * side;
                        float pm  = OAR_PIVOT * olen;                       // model-space: from the oar's middle toward the loom (+x)
                        float cq = cosf(oi.fx2), sq = sinf(oi.fx2);
                        float mx = pm * cq, my = pm * sq;                   // after the pitch about the model's z
                        float ch = cosf(oi.heading), sh2 = -sinf(oi.heading);   // model +x -> this world direction
                        oi.x = rlx - mx * ch; oi.z = rlz - mx * sh2; oi.y -= my;
                    }
                    // 0.22.25: the probe, rebuilt on the asset's **loaded** bounds instead of
                    // the raw GLB's, so it measures what is actually drawn.
                    { static int said = 0;
                      if (!said && i == 0 && side > 0) { said = 1;
                        float cs = cosf(oi.fx2), sn = sinf(oi.fx2);
                        float e0 = oar->bmin[0] * cscale, e1 = oar->bmax[0] * cscale;   // the two ends on the model's x
                        float ch2 = cosf(oi.heading), sh3 = -sinf(oi.heading);
                        float ax0 = e0 * cs, ay0 = e0 * sn, ax1 = e1 * cs, ay1 = e1 * sn;
                        float w0x = oi.x + ax0 * ch2, w0z = oi.z + ax0 * sh3, w0y = oi.y + ay0;
                        float w1x = oi.x + ax1 * ch2, w1z = oi.z + ax1 * sh3, w1y = oi.y + ay1;
                        LOG_INFO("gfx", "oar check: hull %.2f, oar %.2f (%.0f%% of hull), rail %.2f; -x end lateral %.2f height %.2f; +x end lateral %.2f height %.2f (deck %.2f)",
                                 len, olen, 100.f * olen / maxf(len, 1e-3f), half,
                                 (w0x - r->x) * sx + (w0z - r->z) * sz, w0y,
                                 (w1x - r->x) * sx + (w1z - r->z) * sz, w1y, -water + hh * c->deck); } }
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


// Per frame: the instance lists are rebuilt from scratch (scene.c and staged_scene.cc both call
// this before pushing ships), then uploaded and drawn with whatever pipeline is bound.
void Shipyard::frame_begin(void) {
    for (int i = 0; i < NCLASSES; ++i) { instlist_clear(&rig_inst[i]); instlist_clear(&rig_worn_inst[i]); instlist_clear(&cloth_inst[i]); }
    for (int v = 0; v < 2; ++v) instlist_clear(&lateen_cloth_inst[v]);
    for (int q = 0; q < SQ_KINDS; ++q) for (int v = 0; v < 2; ++v) instlist_clear(&sq_cloth_inst[q][v]);
    for (int i = 0; i < FLAG_MAX; ++i) flags[i].drawn = 0;
    shipyard_rigged = 0; shipyard_flags_live = 0; shipyard_yards_authored = 0;
}

void Shipyard::upload(void) {
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_set_instances(&rig_cache[i].rope,      rig_inst[i].v,      rig_inst[i].n);
        mesh_set_instances(&rig_cache[i].rope_worn, rig_worn_inst[i].v, rig_worn_inst[i].n);
        mesh_set_instances(&rig_cache[i].cloth,     cloth_inst[i].v,    cloth_inst[i].n);
    }
    if (lateen_cloth_built) for (int v = 0; v < 2; ++v) mesh_set_instances(&lateen_cloth_mesh[v], lateen_cloth_inst[v].v, lateen_cloth_inst[v].n);
    if (sq_cloth_built) for (int q = 0; q < SQ_KINDS; ++q) for (int v = 0; v < 2; ++v) mesh_set_instances(&sq_cloth_mesh[q][v], sq_cloth_inst[q][v].v, sq_cloth_inst[q][v].n);
    for (int i = 0; i < FLAG_MAX; ++i) if (flags[i].drawn && flags[i].built) mesh_set_instances(&flags[i].mesh, &flags[i].inst, 1);
}

// The main pass: canvas and spars through the bound mesh pipeline; the cordage through the
// rope pipeline (line geometry, geometry stage) when there is one, the mesh pipeline restored
// afterwards; the simulated flags last.
int Shipyard::submit(void) {
    int n = 0;
    int lines = meshes_rope_available();
    if (lines) meshes_bind_rope();
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_draw(&rig_cache[i].rope);      n += rig_cache[i].rope.ninst;
        mesh_draw(&rig_cache[i].rope_worn); n += rig_cache[i].rope_worn.ninst;
    }
    if (lines) meshes_bind();
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        mesh_draw(&rig_cache[i].cloth); n += rig_cache[i].cloth.ninst;
    }
    if (lateen_cloth_built) for (int v = 0; v < 2; ++v) { mesh_draw(&lateen_cloth_mesh[v]); n += lateen_cloth_mesh[v].ninst; }
    if (sq_cloth_built) for (int q = 0; q < SQ_KINDS; ++q) for (int v = 0; v < 2; ++v) { mesh_draw(&sq_cloth_mesh[q][v]); n += sq_cloth_mesh[q][v].ninst; }
    for (int i = 0; i < FLAG_MAX; ++i) if (flags[i].drawn && flags[i].built) { mesh_draw(&flags[i].mesh); n++; }
    return n;
}

// The shadow pass (depth pipeline bound): canvas and spars cast; line ropes cannot be drawn by
// a triangle pipeline and would not show in a shadow texel anyway, the flags likewise.
int Shipyard::submit_shadow(void) {
    int n = 0;
    int lines = meshes_rope_available();
    for (int i = 0; i < NCLASSES; ++i) {
        if (!rig_cache[i].built) continue;
        if (!lines) { mesh_draw(&rig_cache[i].rope); n += rig_cache[i].rope.ninst;
                      mesh_draw(&rig_cache[i].rope_worn); n += rig_cache[i].rope_worn.ninst; }
        mesh_draw(&rig_cache[i].cloth); n += rig_cache[i].cloth.ninst;
    }
    if (lateen_cloth_built) for (int v = 0; v < 2; ++v) { mesh_draw(&lateen_cloth_mesh[v]); n += lateen_cloth_mesh[v].ninst; }
    if (sq_cloth_built) for (int q = 0; q < SQ_KINDS; ++q) for (int v = 0; v < 2; ++v) { mesh_draw(&sq_cloth_mesh[q][v]); n += sq_cloth_mesh[q][v].ninst; }
    return n;
}

} // namespace mare::game::view

// C ABI (shipyard.h) for the unported callers
extern "C" {
int  shipyard_push(const shipyard_req_t *r) { return mare::game::view::shipyard().push(r); }
void shipyard_request_all(void) { mare::game::view::shipyard().request_all(); }
void shipyard_frame_begin(void) { mare::game::view::shipyard().frame_begin(); }
void shipyard_upload(void) { mare::game::view::shipyard().upload(); }
int  shipyard_submit(void) { return mare::game::view::shipyard().submit(); }
int  shipyard_submit_shadow(void) { return mare::game::view::shipyard().submit_shadow(); }
const char *shipyard_class_name(int type, int nation) { return mare::game::view::shipyard().class_name(type, nation); }
int  shipyard_class_stats(int type, int nation, ship_stats_t *out) { return mare::game::view::shipyard().class_stats(type, nation, out); }
}
