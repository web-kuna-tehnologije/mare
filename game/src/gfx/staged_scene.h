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

#ifdef __cplusplus
extern "C" {
#endif

enum { DV_OFF = 0, DV_BATTLE, DV_CITY };

int  detailview_mode(void);                    // DV_OFF when inactive (world renders as usual)
int  detailview_busy(void);                    // clouds are covering the screen (input besides Esc ignored)
void detailview_enter_battle(int fleet_id);    // around this fleet's battle; 0 = freshest battle, staged exercise if none
void detailview_enter_city(int port);          // -1 = nearest living town
void detailview_exit(void);                    // back through the clouds to the map
void detailview_abort(void);                   // drop the scene at once (the menu's blurred battle gives way to the game, 0.22.11)
int  detailview_hits(void);                    // round shot that struck a hull this battle (ballistics, 0.22.11)
void detailview_frame(float dt);               // animate + upload instances (before the shadow pass)
int  detailview_submit(void);                  // draw stage + instances (main pass)
int  detailview_submit_shadow(void);           // the same scene for a shadow cascade (coarse caster meshes)
int  detailview_submit_smoke(void);            // gunsmoke, after the translucent bind
void detailview_sea(void);                     // the staged scene's water (same shader and waves as the map)
void detailview_clouds(void);                  // cloud overlay, after hdr_resolve (no-op when clear)
// The battle clock, the wind and the coast (0.22.7, issue4). The battle runs on its own clock:
// Space stops it, 1-4 set it, -/+ halve and double it; the strategic clock is untouched.
float detailview_timescale(void);              // the battle clock, 1 = real time, 0 = stopped
void  detailview_set_timescale(float s);
float detailview_clock_scale(void);            // what the scene clock (RF.anim_time) and the wave clocks advance by: the battle's timescale in the battle, else 1
void  detailview_wind(float *dx, float *dz, float *str);   // the wind at the battle (world direction it blows towards, strength ~0.3..1.4)
int   detailview_show_wind(void);              // wind arrows on?
void  detailview_toggle_wind(void);
const char *detailview_coast_desc(void);       // "wooded hills to the NE, 12 km" / "open sea ..."
void  detailview_overlays(void);               // wind arrows (dd lines; the caller flushes)
void  detailview_weather(void);                // after the weather: haze for the open sea, a little for a coast
float detailview_eye_floor(void);              // the lowest the eye may sit in a staged scene (local units)
void  detailview_trailer(float t);             // the trailer's keyed camera, at t seconds into the take (main.cc MARE_RECORD; < 0 holds the first mark)

#ifdef __cplusplus
}
#endif

#endif
