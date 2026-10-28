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

typedef struct weather_t { float calm, haze, mist, overcast, rain, storm; } weather_t;
extern weather_t WX;                 // the smoothed state this frame
extern int weather_enabled;

void weather_update(float dt);       // once per frame, after sea_update_waves and before render_light_update: writes RF.fog_*, tints RF.sky
void weather_overlay(void);          // rain, after the tonemap (full-screen, alpha)

#endif
