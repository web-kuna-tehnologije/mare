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
// 0.22.17 (P1.3): where the scene's origin sits in the world. The particles are drawn in world
// coordinates but the tactical bodies live in the staged scene's own frame, so everything this
// module hands to sim/naval_physics - the shot and the recoil - is taken back to that frame
// first. Without it no ball in any battle could match a hull: the broadphase compared a gun at
// world (1000, -700) with a body at local (-40, 20) and never found anything to hit.
void particles_set_origin(float ox, float oz);
// One broadside: `side` is +1 / -1 for the ship's starboard / port (in the heading frame, the
// convention the staged ships use), `hull_len` the hull length in local units, `guns` the
// class's total gun count (half fire); puff size and count follow both.
// `body` is her rigid body (sim/naval_physics.h) for the recoil and the shot, or -1.
void particles_broadside(float x, float y, float z, float heading, float side, float hull_len, int guns, int body);
// A ship on fire (0.22.19, todo 152): a column of dark smoke with flame in its root, rising
// from her and leaning away on the wind. `heat` is how far gone she is, 0..1 - it decides how
// black the smoke is, how fast it climbs and how much flame shows at the base.
void particles_fire(float x, float y, float z, float hull_len, float heat);
// ... and the end of one (NPHYS_EV_EXPLODE): the magazine, all at once.
void particles_explosion(float x, float y, float z, float hull_len);
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
