// Scene assembly: decides per frame what to draw and at which LOD.
//
//   ships:  < 75 units full mesh (all ships in line-ahead), < 230 simplified (max 4), else icon
//   ports:  < 260 units procedural town (houses by population, tower, fort walls, pier, flag), else icon
//   map view (mapness > 0.5): everything becomes icons; trade lanes are drawn
// Towns are regenerated from a per-port seed each frame (cheap, always in sync with the state).

#ifndef MARE_SCENE_H
#define MARE_SCENE_H

#include "../core/types.h"

typedef struct scene_hints_t {   // filled by the UI each frame before drawing
    int sel_fleet_id, hover_fleet_id, sel_port, hover_port;
    int route_edit;              // the selected fleet's route is being edited (draw it green, open)
    int show_lanes, show_wind, show_regions;
} scene_hints_t;
extern scene_hints_t SH;

void scene_fleet_pos(const fleet_t *f, float *x, float *z, float *heading);   // draw position (docked fleets are moored around the harbour)
void scene_reset_caches(void);    // call after game_new/game_load: town instance caches depend on world heights and port state
void scene_draw_ports(void);      // after meshes_bind(); towns are cached per port and rebuilt when size/fort/owner/population change
void scene_draw_ships(void);      // after meshes_bind()
void scene_push_icons(void);      // icons for far/map-view ports, fleets and storms (between icons_begin/icons_draw)
void scene_draw_overlays(void);   // debug-draw lines: selected path, routes, lanes, storms, wind arrows (flushed by dd_flush)

#endif
