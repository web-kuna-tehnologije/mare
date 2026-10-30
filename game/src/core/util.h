// Small helpers shared by every module: math, coordinates, rng, calendar, colors.

#ifndef MARE_UTIL_H
#define MARE_UTIL_H

#include <math.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float minf(float a, float b) { return a < b ? a : b; }
static inline int   maxi(int a, int b) { return a > b ? a : b; }
static inline int   mini(int a, int b) { return a < b ? a : b; }
static inline float sqr(float x) { return x * x; }
static inline float dist2d(float ax, float az, float bx, float bz) { return sqrtf(sqr(ax-bx)+sqr(az-bz)); }
static inline float wrap_angle(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// geo <-> world
static inline float lon2x(float lon) { return lon * WORLD_K; }
static inline float lat2z(float lat) { return -lat * WORLD_K; }
static inline float x2lon(float x) { return x / WORLD_K; }
static inline float z2lat(float z) { return -z / WORLD_K; }

// land mask cell <-> geo (row 0 = north)
static inline int   lon2cx(float lon) { return (int)((lon - LON0) * CELLS_PER_DEG); }
static inline int   lat2cy(float lat) { return (int)((LAT1 - lat) * CELLS_PER_DEG); }
static inline float cx2lon(int cx)    { return LON0 + (cx + 0.5f) / CELLS_PER_DEG; }
static inline float cy2lat(int cy)    { return LAT1 - (cy + 0.5f) / CELLS_PER_DEG; }

// deterministic rng (xorshift32). a zero state would be stuck forever, so it is repaired.
static inline unsigned rnd_u(unsigned *s) { unsigned x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static inline float    rnd_f(unsigned *s) { return (rnd_u(s) & 0xFFFFFF) / 16777216.f; }
static inline float    rnd_range(unsigned *s, float a, float b) { return a + (b-a) * rnd_f(s); }
static inline int      rnd_int(unsigned *s, int n) { return n <= 0 ? 0 : (int)(rnd_u(s) % (unsigned)n); }
// probability `p_per_day` evaluated for a step of `dt` days
static inline int      rnd_chance(unsigned *s, float p_per_day, float dt) { return rnd_f(s) < p_per_day * dt; }

// calendar (proleptic gregorian, from 1 Jan 1620)
static inline void day2date(float day, int *y, int *m, int *d) {
    static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int year = 1620, di = (int)day; if (di < 0) di = 0;
    for (;;) { int leap = (year%4==0 && (year%100!=0 || year%400==0)); int yl = 365 + leap; if (di < yl) break; di -= yl; year++; }
    int leap = (year%4==0 && (year%100!=0 || year%400==0));
    int mon = 0; for (;;) { int ml = mdays[mon] + (mon==1 && leap); if (di < ml) break; di -= ml; mon++; }
    *y = year; *m = mon + 1; *d = di + 1;
}
static inline int day2year(float day)  { int y,m,d; day2date(day,&y,&m,&d); return y; }
static inline int day2month(float day) { int y,m,d; day2date(day,&y,&m,&d); return m; }

// colors: 0xAABBGGRR (what v3's palette/dd use)
#define RGB_(r,g,b) (0xFF000000u | ((unsigned)(b)<<16) | ((unsigned)(g)<<8) | (unsigned)(r))
static inline unsigned rgb(unsigned r, unsigned g, unsigned b) { return RGB_(r,g,b); }
static inline float col_r(unsigned c) { return ((c>>0)&255)/255.f; }
static inline float col_g(unsigned c) { return ((c>>8)&255)/255.f; }
static inline float col_b(unsigned c) { return ((c>>16)&255)/255.f; }

#ifdef __cplusplus
}
#endif

#endif
