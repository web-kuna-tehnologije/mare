// Authored ships (game/docs/DESIGN_SHIPS.md): assembles a ship from the imported Meshy parts -
// a hull, a mast assembly and, for the oared Mediterranean interceptors, a bank of oars - and
// submits them through the normal authored-asset path (scene_asset_instance), so they get LOD
// selection, cross-fades, residency, shadows and wave motion exactly like every other asset.
//
// Every game ship type maps to a Gen I class from the design sheets, with a Mediterranean
// flavour for Venice, the Ottomans and the pirates (who sail the oared fusta and galiot). When
// a class's hull has not finished loading - or was never imported - shipyard_push returns 0 and
// the caller falls back to the procedural hull in meshes.c, so ships never disappear.

#ifndef MARE_SHIPYARD_H
#define MARE_SHIPYARD_H

#include "gpu.h"

typedef struct shipyard_req_t {
    int      type, nation, role;    // SHIP_*, NATION_*, ROLE_*
    float    x, z, heading;         // world position and heading (radians, as fleet_t uses)
    float    rock, sel, dither;     // the instance fields the procedural hull would have taken
    float    cr, cg, cb;            // nation colour
    float    camdist;               // for LOD selection
    unsigned key;                   // stable per-ship LOD key (lod_key(LODK_FLEET, id, slot))
} shipyard_req_t;

// Submit one ship. Returns 1 when an authored hull was submitted (the caller draws nothing
// else), 0 when the class is unavailable and the procedural mesh should be used instead.
int  shipyard_push(const shipyard_req_t *r);

// Start loading every class's parts (called once the renderer is up, so the first battle does
// not wait on a cold asset cache).
void shipyard_request_all(void);

const char *shipyard_class_name(int type, int nation);   // dashboard / logs
extern int shipyard_enabled;        // MARE_SHIPS=0 forces the procedural hulls (A/B comparison)
extern int shipyard_oars_drawn;     // oar instances submitted this frame (dashboard)
extern int shipyard_ships_drawn;    // authored ships submitted this frame
extern int shipyard_fallbacks;      // ships that fell back to the procedural hull this frame

#endif
