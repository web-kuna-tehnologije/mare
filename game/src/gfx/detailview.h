// Detail view: staged, spectator-only close-up scenes entered through a cloud transition -
// a sea battle a kilometre off a coast with every ship at true relative scale and sailing on
// its own, or a 1:1 town laid out in districts that follow the port's size and development.
//
// The scene is local: while active, the world passes (terrain, sea, ports, ships, icons) are
// skipped and the detail view submits its own stage + instances through the standard mesh
// pipeline, so PBR, gunsmoke and the shadow cascades all apply. Local units are ~4 m; the
// camera is the ordinary RTS camera driven in that space (pan/rotate/zoom work as usual).
// The simulation keeps running underneath: battles are free to spectate at full speed, the
// city crawls at 0.05 days/s like the map's city view. Esc (or M) returns through the clouds.

#ifndef MARE_DETAILVIEW_H
#define MARE_DETAILVIEW_H

enum { DV_OFF = 0, DV_BATTLE, DV_CITY };

int  detailview_mode(void);                    // DV_OFF when inactive (world renders as usual)
int  detailview_busy(void);                    // clouds are covering the screen (input besides Esc ignored)
void detailview_enter_battle(int fleet_id);    // around this fleet's battle; 0 = freshest battle, staged exercise if none
void detailview_enter_city(int port);          // -1 = nearest living town
void detailview_exit(void);                    // back through the clouds to the map
void detailview_frame(float dt);               // animate + upload instances (before the shadow pass)
int  detailview_submit(void);                  // draw stage + instances (main pass and shadow pass)
int  detailview_submit_smoke(void);            // gunsmoke, after the translucent bind
void detailview_clouds(void);                  // cloud overlay, after hdr_resolve (no-op when clear)

#endif
