// Global wind model: trade winds (blow west, 5-30 deg), horse latitudes, westerlies (blow east,
// 38-65 deg), polar easterlies and equatorial doldrums, with seasonal drift and noise.

#ifndef MARE_WIND_H
#define MARE_WIND_H

#ifdef __cplusplus
extern "C" {
#endif

void  wind_at(float wx, float wz, float day, float *dx, float *dz, float *strength);   // unit direction + strength (~0.3..1.4)
float wind_factor(float hx, float hz, float wdx, float wdz, float wstr); // speed multiplier for heading (hx,hz): 0.45..1.35

#ifdef __cplusplus
}
#endif

#endif
