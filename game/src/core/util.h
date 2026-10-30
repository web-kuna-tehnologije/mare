// The short names every module calls, forwarding to the classes that own them.
//
// The implementations moved to core/util.hh (mare::core::Math / Geo / Rng / Calendar /
// Colour). These one-line forwards exist so that the forty translation units already calling
// clampf(), lon2x() and rnd_f() did not all have to change, and so that the headers which
// include this one inside an `extern "C"` block keep working. There is exactly one copy of
// each algorithm, in the class; nothing below does any work of its own.

#ifndef MARE_UTIL_H
#define MARE_UTIL_H

#include <math.h>
#include "types.h"

#ifndef __cplusplus
#error core/util.h now forwards to the C++ classes in core/util.hh; the game has no C units left
#endif

#include "util.hh"

extern "C" {

static inline float clampf(float v, float a, float b) { return mare::core::Math::clamp(v, a, b); }
static inline float lerpf(float a, float b, float t) { return mare::core::Math::lerp(a, b, t); }
static inline float maxf(float a, float b) { return mare::core::Math::max(a, b); }
static inline float minf(float a, float b) { return mare::core::Math::min(a, b); }
static inline int   maxi(int a, int b) { return mare::core::Math::maxi(a, b); }
static inline int   mini(int a, int b) { return mare::core::Math::mini(a, b); }
static inline float sqr(float x) { return mare::core::Math::sqr(x); }
static inline float dist2d(float ax, float az, float bx, float bz) { return mare::core::Math::dist2d(ax, az, bx, bz); }
static inline float wrap_angle(float a) { return mare::core::Math::wrap_angle(a); }

// geo <-> world
static inline float lon2x(float lon) { return mare::core::Geo::lon2x(lon); }
static inline float lat2z(float lat) { return mare::core::Geo::lat2z(lat); }
static inline float x2lon(float x) { return mare::core::Geo::x2lon(x); }
static inline float z2lat(float z) { return mare::core::Geo::z2lat(z); }

// land mask cell <-> geo (row 0 = north)
static inline int   lon2cx(float lon) { return mare::core::Geo::lon2cx(lon); }
static inline int   lat2cy(float lat) { return mare::core::Geo::lat2cy(lat); }
static inline float cx2lon(int cx)    { return mare::core::Geo::cx2lon(cx); }
static inline float cy2lat(int cy)    { return mare::core::Geo::cy2lat(cy); }

// deterministic rng (xorshift32); the simulation's replay depends on it
static inline unsigned rnd_u(unsigned *s) { return mare::core::Rng::advance(s); }
static inline float    rnd_f(unsigned *s) { return mare::core::Rng::unit(s); }
static inline float    rnd_range(unsigned *s, float a, float b) { return mare::core::Rng::range(s, a, b); }
static inline int      rnd_int(unsigned *s, int n) { return mare::core::Rng::integer(s, n); }
static inline int      rnd_chance(unsigned *s, float p_per_day, float dt) { return mare::core::Rng::chance(s, p_per_day, dt); }

// calendar (proleptic gregorian, from 1 Jan 1620)
static inline void day2date(float day, int *y, int *m, int *d) { mare::core::Calendar::date(day, y, m, d); }
static inline int  day2year(float day)  { return mare::core::Calendar::year(day); }
static inline int  day2month(float day) { return mare::core::Calendar::month(day); }

// colors: 0xAABBGGRR (what v3's palette/dd use)
#define RGB_(r,g,b) (0xFF000000u | ((unsigned)(b)<<16) | ((unsigned)(g)<<8) | (unsigned)(r))
static inline unsigned rgb(unsigned r, unsigned g, unsigned b) { return mare::core::Colour::pack(r, g, b); }
static inline float col_r(unsigned c) { return mare::core::Colour::red(c); }
static inline float col_g(unsigned c) { return mare::core::Colour::green(c); }
static inline float col_b(unsigned c) { return mare::core::Colour::blue(c); }

}

#endif
