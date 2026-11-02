// Naval physics foundation (0.22.9-0.22.11, dev.txt P1.3).
//
// A small fixed-step rigid-body world for the *tactical* scale - the staged battle, where
// ships are twenty units long and you can see them touch. The strategic simulation is
// untouched: it stays discrete, one day a tick.
//
//   1. Fixed physics timestep independent of render FPS. nphys_advance(dt) accumulates real
//      time and runs whole NPHYS_STEP steps; the state after N steps does not depend on how
//      the caller sliced the time, which is what naval_physics_test proves.
//   2. Tactical naval fixed sub-step. Every step integrates drive, drag, buoyancy, shot and
//      collision in the same order, so a battle at 30 fps and at 240 fps are the same battle.
//   3. Stable rigid-body transforms. Each body keeps the previous step's pose as well as the
//      current one and nphys_pose() interpolates between them by the leftover accumulator.
//   4. Simplified compound collision hulls: three discs down the keel, radius from the beam.
//   5. Ship-to-ship collision: disc pairs, positional correction by inverse mass, restitution,
//      sideways friction, and the yaw the lever arm gives.
//   6. Ship-to-coast collision: the staged shore profile as a signed distance.
//   7. (0.22.11) Multi-point buoyancy: the caller's wave function sampled under the three
//      discs and the two bilges gives heave, pitch and roll per body (nphys_attitude).
//   8. (0.22.11) Ballistics: shot as swept spheres under gravity, tested against the compound
//      hulls each step; hits and splashes come back as events (nphys_poll_events).
//   9. (0.22.11) Impulses at a point (nphys_impulse): the recoil of a gun pushes the ship the
//      other way and, off her centre, turns her - Newton's third law and the lever arm.
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
#define NPHYS_MAX_SHOTS  512
#define NPHYS_MAX_EVENTS 256

void nphys_reset(void);                // drop every body, shot and event (a new scene)

// Add a hull. `tonnes` is her displacement, `len`/`beam`/`draft` her size in the caller's units.
// Returns the body id, or -1 when the world is full.
int  nphys_add(float x, float z, float heading, float len, float beam, float draft, float tonnes);

// What the helm is asking for this frame: a speed through the water, a course to steer, and
// the best rate of turn the class can manage (radians per second).
void nphys_drive(int id, float speed_cmd, float heading_cmd, float turn_rate_max);

// The shore, in the staged scene's own terms (staged_scene.cc sea_local_shore_*): a point on
// the shoreline, the unit normal pointing inland, the wiggle amplitude and frequency, and the
// seabed slope (depth per unit seaward). on = 0 for open water.
void nphys_set_coast(float ax, float az, float nx, float nz, float wiggle_amp, float wiggle_freq, float slope, int on);

// The water surface the bodies float on: height at (x, z) in the caller's units. NULL = flat.
// Sampled under five points of each hull every step (item 7).
typedef float (*nphys_surface_fn)(float x, float z, void *user);
void nphys_set_surface(nphys_surface_fn fn, void *user);

// An impulse (mass x velocity, in the caller's units) applied at a world point: the recoil of
// a broadside, a ram, a tow. The off-centre part turns her (item 9).
void nphys_impulse(int id, float px, float pz, float jx, float jz);
// A kick to her roll (radians of heel), as a gun's recoil at deck height gives: a damped
// spring brings her back over a few seconds (0.22.12). Starboard down +.
void nphys_kick_roll(int id, float rad);

// A round shot: from a point at height y, along a unit direction with a muzzle speed, from
// `owner` (never hits her). Falls under gravity, tested against every other hull each step.
//
// 0.22.19 (issue15 "add real cannonballs in game and sizes depending of cannons ... also add
// air resistance to slow down where it hits"). `radius` is the ball's true radius in scene
// units, and it is not decoration: the drag deceleration of a sphere is
//
//     a = 3 * rho_air * Cd * v^2 / (8 * rho_iron * r)   ~=  2.75e-5 * v^2 / r
//
// which is inversely proportional to the radius, so a 6-pounder's ball loses its way far
// faster than a 32-pounder's and the small guns are genuinely short-ranged. (The constant is
// the same in scene units as in metres: the scale cancels between v^2 and r.) At a 32-pdr's
// 450 m/s that is about 8 g of deceleration, which is what round shot really does.
//
// `drift` is a constant sideways acceleration, the curve a smooth-bore ball takes from
// windage and spin - the reason a gun captain could not simply point at his target.
void nphys_fire(int owner, float x, float y, float z, float dx, float dy, float dz, float speed,
                float radius, float drift);

// Run whole fixed steps for `dt` seconds of scene time. dt <= 0 holds everything still.
void nphys_advance(float dt);

// The pose to draw this frame: the fixed-step state interpolated by the leftover accumulator.
void nphys_pose(int id, float *x, float *z, float *heading);
void nphys_velocity(int id, float *vx, float *vz, float *yaw_rate);
// heave (units above the flat surface), pitch (radians, bow up +), roll (radians, starboard down +)
void nphys_attitude(int id, float *heave, float *pitch, float *roll);
float nphys_speed(int id);              // through the water, forward component
float nphys_heel(int id);               // 0..1: how hard she is leaning into her turn (the renderer's roll scale)
int   nphys_aground(int id);            // 1 while a hull disc is in water shallower than her draft
void  nphys_teleport(int id, float x, float z, float heading);   // formation setup: no interpolation from the old pose

// Events since the last poll: what the shot did. Returns the count written (<= max).
typedef struct nphys_event_t {
    int   kind;                // NPHYS_EV_SPLASH (into the sea), NPHYS_EV_HIT (a hull)
    int   body;                // the hull hit, or -1
    int   owner;               // who fired
    float x, y, z;             // where
    float speed;               // of the shot at that moment
} nphys_event_t;
enum { NPHYS_EV_SPLASH = 1, NPHYS_EV_HIT = 2, NPHYS_EV_MAST_DOWN = 3, NPHYS_EV_SUNK = 4,
       NPHYS_EV_FIRE = 5,        // 0.22.19 (todo 152): she has caught light
       NPHYS_EV_EXPLODE = 6,     // ... and it reached the magazine
       NPHYS_EV_STRANDED = 7 };  // 0.22.20 (issue16): hard aground long enough to be stuck fast

// ---------------------------------------------------------------------------
// Damage (0.22.17, P1.3 "component / zone damage model"). Every hit lands somewhere on the
// hull and what it breaks follows from *where*: between wind and water it starts her leaking,
// on the gun deck it dismounts guns, aloft it wounds the rig. Four scalars 0..1 carry it, and
// flooding is the one that kills her - it grows with the holes below the line, drags her down
// by the head and, past NP_SINK_AT, takes her under.
typedef struct nphys_damage_t {
    float hull;      // 0 sound .. 1 a sieve
    float rig;       // masts, yards and canvas: what she can still set
    float guns;      // the battery she can still fire
    float flood;     // water in her: 0 dry .. 1 gone
    float fire;      // 0.22.19 (todo 152): how far she is alight, 0 .. 1
    int   sunk;      // she has gone under; the body stops driving and is left to settle
    int   stranded;  // 0.22.20 (issue16): stuck fast on the ground - no helm, no way
    int   masts_down;// masts carried away so far (each is an event)
    int   blew_up;   // 0.22.19: the magazine went; she is gone at once, not by degrees
} nphys_damage_t;

void nphys_damage_get(int id, nphys_damage_t *out);   // zeros when the id is not a body
// A blow dealt by something other than round shot - a ram, a grounding, a fire below:
// `where` is 0 below the waterline, 1 on the gun deck, 2 aloft.
void nphys_damage_add(int id, int where, float amount);
float nphys_speed_factor(int id);   // 1 sound .. ~0.25 a wreck: what her rig and her flooding leave her
int   nphys_sunk(int id);
int   nphys_poll_events(nphys_event_t *out, int max);

// diagnostics
int   nphys_steps_last(void);           // fixed steps run by the last advance()
int   nphys_contacts_last(void);        // ship-ship contacts resolved in the last step
int   nphys_shots(void);                // shot in the air
int   nphys_wrecks(void);               // bodies that have gone under this scene
int   nphys_shot_list(float *xyzr, int max);  // x, y, z and the ball's radius per shot (0.22.19: four floats, was three); returns the count written
float nphys_alpha(void);                // interpolation fraction, 0..1
int   nphys_count(void);

#ifdef __cplusplus
}
#endif

#endif
