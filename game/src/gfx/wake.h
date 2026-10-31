// The wake map (0.22.9, issue6 "add trail of foam after ship that fades away and leave waves
// as ship creates them").
//
// The 0.22.6 Kelvin wake is analytic and *attached* to the hull: it is correct right behind
// her and it turns with her, so a ship that wears round drags her whole wake with her and
// nothing is left on the water where she actually was. A trail has to be written into the
// world and then forgotten slowly, which is what this is: one R8-ish target covering a square
// of sea around the staged battle, into which every ship paints her churn each frame, and
// which fades towards nothing with a time constant of about a minute. The sea's vertex stages
// read it as a slight flattening (the water she has already broken) and the fragment stage as
// foam, so the trail *is* the sea's own surface rather than a decal over it.
//
// Only the staged scenes use it: there the scene centre is fixed, so the map never has to
// scroll, and a texel is a couple of metres. On the strategic map a ship is a pixel and the
// analytic wake is all that is wanted.

#ifndef MARE_WAKE_H
#define MARE_WAKE_H

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start (or move) the mapped region: a square of `half` world units about (cx, cz). Clears
// when the region moves. half <= 0 switches the map off.
void wake_set_region(float cx, float cz, float half);

// Fade by `dt` seconds (the scene clock: a paused battle holds its wake) and paint the ships
// given in the sea_set_ships layout - x, z, heading, speed in hull lengths per second, plus
// the hull length. Call after sea_set_ships, before the sea draws.
void wake_update(float dt, const float *xzhs, const float *len, int n);

int   wake_active(void);
const rhi_texture_t *wake_texture(void);     // NULL when off
void  wake_rect(float out[4]);               // x0, z0, 1/extent, 1/extent (world -> uv)

#ifdef __cplusplus
}
#endif

#endif
