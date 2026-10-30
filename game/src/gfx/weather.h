// Weather (DESIGN_VISUAL.md step 7): what the air is doing, derived every frame from the wind
// at the look-at point, the storm events near it and the time of day, and handed to the
// renderer as a few numbers.
//
//   calm      the wind has dropped (doldrums, the lee of a storm): a glassy sea, long reflections
//   haze      visibility: fog starts closer and thickens (calm air holds its moisture; storms
//             bring rain and spray)
//   mist      calm + dawn + near a shore: white fog in the bays for an hour after sunrise
//   overcast  a storm overhead: the sun dims, the sky greys
//   rain      streaks over the frame, slanted by the wind, under a storm
//
// Everything is a 0..1 factor smoothed over a few seconds so weather drifts in rather than
// snapping when the camera crosses a storm's edge. MARE_WEATHER=0 disables it,
// MARE_WEATHER=clear|calm|mist|storm pins a state for captures.

#ifndef MARE_WEATHER_H
#define MARE_WEATHER_H

#ifdef __cplusplus
extern "C" {
#endif

// Ported to C++ (src/game/weather.cc, a mare::engine::Entity): the update runs in the
// FrameSetup phase, the rain overlay in the Overlay phase, both driven by the entity
// manager (engine/entities.h). Only the shared state remains a C symbol, for the readers
// that are not ported yet (sky.c, ui/dashboard.c).
typedef struct weather_t { float calm, haze, mist, overcast, rain, storm; } weather_t;
extern weather_t WX;                 // the smoothed state this frame
extern int weather_enabled;

#ifdef __cplusplus
}
#endif

#endif
