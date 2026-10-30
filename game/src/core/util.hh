// mare::core - the small shared helpers, as the classes that own them.
//
// These were loose inline functions in util.h (clampf, lon2x, rnd_f, day2date, col_r...).
// The implementations live here now, grouped by what they are for, and util.h keeps the short
// C names as one-line forwards so the forty translation units that call them did not have to
// move. Everything is a static member of a class with no state: these are pure functions of
// their arguments, and the grouping is what makes them findable - Geo for the coordinate
// conversions that are easy to get backwards, Rng for the deterministic stream the simulation
// depends on, Calendar for the one date algorithm in the game.
//
// Header-only and dependency-free beyond core/types.h, so core, world and sim keep compiling
// standalone (that is what tests/sim_test.cpp relies on).

#ifndef MARE_CORE_UTIL_HH
#define MARE_CORE_UTIL_HH

#ifndef __cplusplus
#error core/util.hh is the C++ home of the helpers; C translation units cannot use it
#endif

#include <math.h>
#include "types.h"

namespace mare::core {

class Math {
public:
    static inline float clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
    static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
    static inline float max(float a, float b) { return a > b ? a : b; }
    static inline float min(float a, float b) { return a < b ? a : b; }
    static inline int   maxi(int a, int b) { return a > b ? a : b; }
    static inline int   mini(int a, int b) { return a < b ? a : b; }
    static inline float sqr(float x) { return x * x; }
    static inline float dist2d(float ax, float az, float bx, float bz) { return sqrtf(sqr(ax - bx) + sqr(az - bz)); }
    // shortest signed angle to the same direction, in (-pi, pi]
    static inline float wrap_angle(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
};

// geo <-> world <-> land-mask cell. x = lon * WORLD_K, z = -lat * WORLD_K, row 0 = north.
class Geo {
public:
    static inline float lon2x(float lon) { return lon * WORLD_K; }
    static inline float lat2z(float lat) { return -lat * WORLD_K; }
    static inline float x2lon(float x) { return x / WORLD_K; }
    static inline float z2lat(float z) { return -z / WORLD_K; }
    static inline int   lon2cx(float lon) { return (int)((lon - LON0) * CELLS_PER_DEG); }
    static inline int   lat2cy(float lat) { return (int)((LAT1 - lat) * CELLS_PER_DEG); }
    static inline float cx2lon(int cx) { return LON0 + (cx + 0.5f) / CELLS_PER_DEG; }
    static inline float cy2lat(int cy) { return LAT1 - (cy + 0.5f) / CELLS_PER_DEG; }
};

// Deterministic xorshift32. The simulation's reproducibility rests on this: the state lives
// in game_t and a save replays bit-identically, so the algorithm must never change silently.
// A zero state would be stuck forever, so it is repaired on use.
class Rng {
public:
    static inline unsigned advance(unsigned *s) { unsigned x = *s ? *s : 0x9E3779B9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
    static inline float    unit(unsigned *s) { return (advance(s) & 0xFFFFFF) / 16777216.f; }
    static inline float    range(unsigned *s, float a, float b) { return a + (b - a) * unit(s); }
    static inline int      integer(unsigned *s, int n) { return n <= 0 ? 0 : (int)(advance(s) % (unsigned)n); }
    // a per-day probability evaluated over a step of `dt` days
    static inline int      chance(unsigned *s, float p_per_day, float dt) { return unit(s) < p_per_day * dt; }
};

// Proleptic Gregorian calendar counting from 1 January 1620.
class Calendar {
public:
    static inline void date(float day, int *y, int *m, int *d) {
        static const int mdays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        int year = 1620, di = (int)day; if (di < 0) di = 0;
        for (;;) { int leap = (year%4==0 && (year%100!=0 || year%400==0)); int yl = 365 + leap; if (di < yl) break; di -= yl; year++; }
        int leap = (year%4==0 && (year%100!=0 || year%400==0));
        int mon = 0; for (;;) { int ml = mdays[mon] + (mon==1 && leap); if (di < ml) break; di -= ml; mon++; }
        *y = year; *m = mon + 1; *d = di + 1;
    }
    static inline int year(float day)  { int y,m,d; date(day,&y,&m,&d); return y; }
    static inline int month(float day) { int y,m,d; date(day,&y,&m,&d); return m; }
};

// packed 0xAABBGGRR, which is what v3's palette and debug draw take
// (pack, not make: the framework defines a function-like macro `make(type, ...)` in
// ext-lab/ext-object/api.h, and `advance`, not next, for the same reason - a method whose
// name collides with one of those expands into nonsense in every translation unit that
// includes both.)
class Colour {
public:
    static inline unsigned pack(unsigned r, unsigned g, unsigned b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }
    static inline float red(unsigned c)   { return ((c >> 0) & 255) / 255.f; }
    static inline float green(unsigned c) { return ((c >> 8) & 255) / 255.f; }
    static inline float blue(unsigned c)  { return ((c >> 16) & 255) / 255.f; }
};

} // namespace mare::core

#endif
