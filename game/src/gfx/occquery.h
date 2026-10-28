// GPU occlusion culling for authored-asset instances (DESIGN_VISUAL.md step 1).
//
// What hides a building in this game is another building: the city and battle tiers put a
// hundred-plus 80k-triangle houses on screen at a 12-28 degree pitch, and the front rows cover
// the back rows. Terrain never does (occlusion.h measured it). So each authored instance is
// tested against the depth buffer the opaque pass just wrote: after the meshes are drawn, its
// bounding box is drawn again with colour and depth writes off inside a hardware occlusion
// query, and the answer ("did any sample pass") arrives a frame or two later. An instance whose
// last answer was "no" is left out of the *main* pass - the shadow pass still draws it, since
// a hidden house still casts a visible shadow - and keeps being queried so it returns the
// moment it shows. Unknown means visible, and so does a moving camera: a frame in which the
// camera translated or turned culls nothing (stale answers would pop), so this pays off in the
// steady views where the triangle load actually hurts, and never shows a hole while panning.
//
// Keys are the same stable per-instance keys the LOD selector uses (lod_key), so an instance
// keeps its answer across frames; the state table is fixed-size with last-seen eviction.

#ifndef MARE_OCCQUERY_H
#define MARE_OCCQUERY_H

#include "gpu.h"
#include "../assets/asset.h"

extern int occq_enabled;                 // dashboard toggle / MARE_OCCQ=0; 2 = issue the queries but never hide (cost measurement)
extern int occq_hidden, occq_queried, occq_tracked, occq_moving, occq_pending;   // this frame (dashboard, metrics)
extern int occq_timeouts;                // queries abandoned unanswered this run

void occq_frame_begin(void);             // collect finished queries, detect camera motion, decide whether culling is active
int  occq_active(void);                  // culling applies this frame (enabled, supported, close tier or staged scene)
int  occq_visible(unsigned key);         // last known answer for this instance; 1 if unknown or the camera moved
void occq_request(unsigned key, const asset_t *a, const inst_t *in);   // query this instance after the opaque pass (once per key per frame)
void occq_flush(void);                   // render thread, after the opaque meshes: draw the proxies inside queries
void occq_reset(void);                   // forget everything (new game / load)

#endif
