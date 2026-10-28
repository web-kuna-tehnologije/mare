// Occlusion culling against the terrain (DESIGN_VISUAL.md step 1).
//
// The world is a heightfield and the camera looks down at it, so the only thing that can hide
// a town, a ship or a terrain chunk is terrain in front of it. That makes occlusion a ray
// question rather than a GPU-query one: march from the object's top towards the eye over the
// heightmap and stop as soon as the ground rises above the ray. Several rays per object (the
// top centre and the top of both lateral extremes as seen from the camera) must all be
// blocked before the object is culled, the ground is read with a margin that grows with the
// distance from the camera (the drawn terrain is LOD-simplified and may sit below the
// heightmap's peaks), and a ray that climbs above the world's highest point stops early - so
// from a high camera almost every test ends after a few samples.
//
// Pure math over a height callback: no GL, no world.h, so asset_test drives it with synthetic
// terrain. The scene passes world_height. Measured over every town from 24 camera placements
// each (tools: the probe in DESIGN_VISUAL.md): the relief here tops out at 7 units and the
// camera never pitches below 12 degrees, so this hides a town in ~1 of 37,000 views and a
// terrain chunk never - it stays because it costs nothing (the ceiling early-out ends almost
// every ray after one sample) and catches the camera-in-a-valley case. The occlusion that pays
// is buildings hiding buildings: occquery.h, GPU queries against the depth buffer.

#ifndef MARE_OCCLUSION_H
#define MARE_OCCLUSION_H

typedef float (*occ_height_fn)(float x, float z);

extern int occlusion_enabled;            // dashboard toggle / MARE_OCCLUSION=0
extern int occ_samples;                  // heightmap reads this frame (dashboard); reset by occ_frame_begin
extern int occ_tests;                    // objects tested this frame

void occ_frame_begin(const float eye[3], float ceiling);   // eye position and the highest terrain point this frame
// The segment from the eye to (px,py,pz) passes under the terrain somewhere beyond `skip` units
// from the point (the object's own footprint). `box` (x0,z0,x1,z1) excludes samples inside it,
// so a terrain chunk cannot hide behind itself; NULL for none.
int  occ_ray_hidden(occ_height_fn h, float px, float py, float pz, float skip, const float *box);
// A vertical cylinder of radius r standing on the ground at (x,z) up to y = top: hidden only
// if its top centre and the tops of both lateral extremes are.
int  occ_sphere_hidden(occ_height_fn h, float x, float z, float r, float top);

#endif
