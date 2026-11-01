// Particle system (0.22.7, issue4): instanced camera-facing billboards with their own
// translucent pipeline - gunsmoke plumes that drift with the wind, muzzle flashes and a few
// embers per gun. Written for the staged battle (local units, ~2.5 m each); the map keeps its
// old sphere puffs (scene.cc), which read better at 7 km per unit.
//
// Smoke is "volumetric" the cheap way: every billboard is a disc eroded by a 3D noise that
// scrolls with the particle's age, lit as a sphere (the sun side bright, the lee side in the
// ambient) with a back-lit rim, faded where it intersects the opaque scene. Position, drift
// and growth are simulated here; nothing is baked.

#ifndef MARE_PARTICLES_H
#define MARE_PARTICLES_H

#ifdef __cplusplus
extern "C" {
#endif

void particles_init(void);                               // pipeline + the quad (after render_init's programs)
void particles_reset(void);                              // drop everything (a new staged scene)
void particles_set_wind(float vx, float vz);             // the air the smoke drifts in, local units per second
// One broadside: `side` is +1 / -1 for the ship's starboard / port (in the heading frame, the
// convention the staged ships use), `hull_len` the hull length in local units, `guns` the
// class's total gun count (half fire); puff size and count follow both.
// `body` is her rigid body (sim/naval_physics.h) for the recoil and the shot, or -1.
void particles_broadside(float x, float y, float z, float heading, float side, float hull_len, int guns, int body);
void particles_splash(float x, float z, float size);                          // a shot into the sea (0.22.11)
void particles_hit(float x, float y, float z, float dx, float dz, float size); // a shot into a hull: splinters, dust, embers
void particles_spray(float x, float y, float z, float dx, float dz, float strength);   // spray torn off a bow in a breeze
void particles_update(float dt);                         // on the battle clock
int  particles_draw(void);                               // binds its own pipeline; after the sea, before the tonemap. Returns instances drawn
int  particles_count(void);

#ifdef __cplusplus
}
#endif

#endif
