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
// The helm (0.22.17, issue14 "Q and E in battle view don't work" - they were never bound).
// `detailview_helm(d)` puts the player's side under manual steering and turns the ordered
// course by `d` radians; `detailview_helm_release()` hands her back to the line. Every ship
// answers at her own rate through her rigid body, so a great carrack comes round slowly.

// Orders to the player's line (0.22.19, issue15 "in battle you are not allowed to make any
// direct changes - add UI with buttons like direct fire to warship / interceptors or
// tradeships, retreat, board or fight to death").
//
// Two independent choices, because they are two different decisions a captain makes:
//
//   * what to shoot at  - DV_FIRE_*: the nearest enemy (the default), the heaviest ship in
//     range (break the enemy's line of battle), or the lightest (take the convoy and let the
//     escort go). The guns pick a target by this every time they are ready to fire.
//   * what to do with the ship - DV_MOVE_*: hold the line, close to board, or haul off.
//     Boarding steers for the nearest enemy and crowds on sail; retreating turns away from
//     the enemy's centre of mass and runs. "Fight to the death" is DV_MOVE_HOLD with the
//     retreat locked out, so the AI cannot haul her off and neither can a stray click.
//
// Both apply to the whole of the player's side, which is the unit the battle actually
// manoeuvres; they are cleared when the scene is left.
enum { DV_FIRE_NEAREST = 0, DV_FIRE_WARSHIPS = 1, DV_FIRE_TRADERS = 2 };
enum { DV_MOVE_LINE = 0, DV_MOVE_BOARD = 1, DV_MOVE_RETREAT = 2, DV_MOVE_LAST_STAND = 3 };
void detailview_set_fire_order(int order);
void detailview_set_move_order(int order);
// 0.22.20 (issue16 "retract sails if upwind"): the player's side takes in or makes sail. Under
// bare poles an oared ship pulls at her oars; a sailing ship is hove to.
void detailview_set_furl_order(int on);
int  detailview_furl_order(void);

// The muster list (0.22.20, issue16 "add ship icons and list on top enemy on bottom player and
// shows stats"). One entry per tactical ship, player's side 0: the HUD draws the player's line
// along the bottom of the battle view and the enemy's along the top, each ship with her
// condition bars. `hull`, `sails`, `guns`, `crew` and `ammo` are what she has LEFT (1 sound
// or full, 0 gone); `water` is how far flooded she is (0 dry); `fire` 0..1. Crew and
// ammunition became real in 0.22.22 (todo 178) and are shown with the rest.
typedef struct dv_shipinfo_t {
    int   side, type;          // 0 = the player's line
    int   sunk, stranded, masts_down;
    float hull, sails, guns, water, fire;
    float crew, ammo;          // 0.22.22 (todo 178): what is left of her people and her powder
} dv_shipinfo_t;
int detailview_ship_info(int i, dv_shipinfo_t *out);   // 0 past the end; i from 0
int detailview_ship_count(void);
int  detailview_fire_order(void);
int  detailview_move_order(void);
void  detailview_helm(float dcourse);
void  detailview_helm_release(void);
int   detailview_helm_on(void);                // 1 while the player's side is steering by hand
float detailview_helm_course(void);            // the ordered course, radians
void  detailview_trailer(float t);             // the trailer's keyed camera, at t seconds into the take (main.cc MARE_RECORD; < 0 holds the first mark)

#ifdef __cplusplus
}
#endif

#endif
