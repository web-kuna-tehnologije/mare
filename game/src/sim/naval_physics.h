// Naval physics foundation (0.22.9, dev.txt P1.3 items 1-6).
//
// A small fixed-step rigid-body world for the *tactical* scale - the staged battle, where
// ships are twenty units long and you can see them touch. The strategic simulation is
// untouched: it stays discrete, one day a tick.
//
//   1. Fixed physics timestep independent of render FPS. nphys_advance(dt) accumulates real
//      time and runs whole NPHYS_STEP steps; the state after N steps does not depend on how
//      the caller sliced the time, which is what naval_physics_test proves.
//   2. Tactical naval fixed sub-step. Every step integrates drive, drag and collision in the
//      same order, so a battle at 30 fps and the same battle at 240 fps are the same battle.
//   3. Stable rigid-body transforms. Each body keeps the previous step's pose as well as the
//      current one and nphys_pose() interpolates between them by the leftover accumulator, so
//      the hulls move smoothly at any frame rate instead of stepping.
//   4. Simplified compound collision hulls: three discs down the keel, radius from the beam.
//      A convex hull of three circles is what a ship is, to the tolerance this needs.
//   5. Ship-to-ship collision: disc pairs, with a positional correction split by inverse mass,
//      a restitution impulse, sideways friction, and the yaw the lever arm gives.
//   6. Ship-to-coast collision: the staged scene's shore profile as a signed distance, so a
//      ship that stands into shoal water grounds, slews and stops instead of sailing through
//      the beach.
//
// Everything is in the caller's own units (the staged scene's local units, ~2.5 m) and in its
// heading convention: heading 0 is +x, and +z is to starboard of it (forward = (cos h, -sin h)).

#ifndef MARE_NAVAL_PHYSICS_H
#define MARE_NAVAL_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NPHYS_HZ        120.f          // the fixed tactical sub-step
#define NPHYS_STEP      (1.f / NPHYS_HZ)
#define NPHYS_MAX_STEPS 8              // per advance(): beyond this the clock is let slip rather than spiralling
#define NPHYS_MAX_BODIES 64
#define NPHYS_PARTS      3             // discs per compound hull

void nphys_reset(void);                // drop every body (a new scene)

// Add a hull. `tonnes` is her displacement, `len`/`beam` her size in the caller's units.
// Returns the body id, or -1 when the world is full.
int  nphys_add(float x, float z, float heading, float len, float beam, float draft, float tonnes);

// What the helm is asking for this frame: a speed through the water, a course to steer, and
// the best rate of turn the class can manage (radians per second).
void nphys_drive(int id, float speed_cmd, float heading_cmd, float turn_rate_max);

// The shore, in the staged scene's own terms (staged_scene.cc sea_local_shore_*): a point on
// the shoreline, the unit normal pointing inland, the wiggle amplitude and frequency, and the
// seabed slope (depth per unit seaward). on = 0 for open water.
void nphys_set_coast(float ax, float az, float nx, float nz, float wiggle_amp, float wiggle_freq, float slope, int on);

// Run whole fixed steps for `dt` seconds of scene time. dt <= 0 holds everything still (a
// paused battle), and the interpolation alpha stays where it was.
void nphys_advance(float dt);

// The pose to draw this frame: the fixed-step state interpolated by the leftover accumulator.
void nphys_pose(int id, float *x, float *z, float *heading);
void nphys_velocity(int id, float *vx, float *vz, float *yaw_rate);
float nphys_speed(int id);              // through the water, forward component
float nphys_heel(int id);               // 0..1: how hard she is leaning into her turn (the renderer's roll scale)
int   nphys_aground(int id);            // 1 while a hull disc is in water shallower than her draft
void  nphys_teleport(int id, float x, float z, float heading);   // formation setup: no interpolation from the old pose

// diagnostics
int   nphys_steps_last(void);           // fixed steps run by the last advance()
int   nphys_contacts_last(void);        // ship-ship contacts resolved in the last step
float nphys_alpha(void);                // interpolation fraction, 0..1
int   nphys_count(void);

#ifdef __cplusplus
}
#endif

#endif
