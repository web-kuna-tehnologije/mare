// Authored ships (game/docs/DESIGN_SHIPS.md): assembles a ship from the imported Meshy parts -
// a hull, one mast part per station and, for the oared Mediterranean interceptors, a bank of
// oars - and submits them through the normal authored-asset path (scene_asset_instance), so
// they get LOD selection, cross-fades, residency, shadows and wave motion exactly like every
// other asset. The canvas, cordage and flags are procedural, built per class from the rig the
// reference sheet gives it (assets/fleet_sails.png), and answer the wind in the mesh shader.
//
// Every game ship type maps to a Gen I class from the design sheets, with a Mediterranean
// flavour for Venice, the Ottomans and the pirates (who sail the oared fusta and galiot). When
// a class's hull has not finished loading - or was never imported - shipyard_push returns 0 and
// the caller falls back to the procedural hull in meshes.c, so ships never disappear.

#ifndef MARE_SHIPYARD_H
#define MARE_SHIPYARD_H

#include "gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct shipyard_req_t {
    int      type, nation, role;    // SHIP_*, NATION_*, ROLE_*
    float    x, z, heading;         // world position and heading (radians, as fleet_t uses)
    float    scale;                 // 1 on the map; the staged scenes draw ships DV_SHIP_SCALE larger
    float    rock, sel, dither;     // the instance fields the procedural hull would have taken
    float    cr, cg, cb;            // nation colour
    float    camdist;               // for LOD selection
    unsigned key;                   // stable per-ship LOD key (lod_key(LODK_FLEET, id, slot))
} shipyard_req_t;

// What a class *is*, physically. The design sheets give the dimensions; the sailing numbers
// are period estimates. Every ship of a class shares them; the staged battle moves ships by
// them (detailview.c) and the buoyancy pass to come reads displacement and draft.
typedef struct ship_stats_t {
    float length_m, beam_m, draft_m;    // overall length, beam, loaded draft
    float tonnes;                       // displacement, loaded
    float knots;                        // best speed under sail, fair wind
    float oar_knots;                    // under oars (0 = not oared)
    float turn_dps;                     // rate of turn, degrees per second, at speed
    int   guns, crew;
} ship_stats_t;

// Submit one ship. Returns 1 when an authored hull was submitted (the caller draws nothing
// else), 0 when the class is unavailable and the procedural mesh should be used instead.
int  shipyard_push(const shipyard_req_t *r);

// Start loading every class's parts (called once the renderer is up, so the first battle does
// not wait on a cold asset cache).
void shipyard_request_all(void);

// Per-frame bracket for the procedural rigging and canvas (one instance per ship of a class).
void shipyard_frame_begin(void);   // before any shipyard_push
void shipyard_upload(void);        // after the last one, before drawing
int  shipyard_submit(void);        // with the instanced mesh pipeline bound

const char *shipyard_class_name(int type, int nation);   // dashboard / logs
// The class's stats for a ship type under a flag. Always fills `out`: a type with no authored
// class gets numbers derived from its gameplay row (data/tables.h). Returns 1 when authored.
int  shipyard_class_stats(int type, int nation, ship_stats_t *out);
extern int shipyard_enabled;        // MARE_SHIPS=0 forces the procedural hulls (A/B comparison)
extern int shipyard_oars_drawn;     // oar instances submitted this frame (dashboard)
extern int shipyard_ships_drawn;    // authored ships submitted this frame
extern int shipyard_fallbacks;      // ships that fell back to the procedural hull this frame
extern int shipyard_rigged;         // ships drawn with rigging and canvas this frame

#ifdef __cplusplus
}
#endif

#endif
