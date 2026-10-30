// Math types for the C++ translation units.
//
// The unported C code uses the v3 framework's vecmath types; C++ code never includes v3.h,
// so these mirror the exact layouts (vec3 = {x,y,z}, mat44 = four vec4 rows) and the values
// cross the C ABI by value, both ways. The static_asserts below are the contract.
//
// Defined at global scope (not in a namespace) because the legacy C headers - render.h,
// camera.h - name them unqualified; mare::core aliases them for new code. A C++ TU must
// include this before any legacy header that uses vec3/mat44, and must not include v3.h.

#ifndef MARE_CORE_MATH_HH
#define MARE_CORE_MATH_HH

#ifdef MATH_H   // v3's ext-vecmath/api.h guard
#error "core/math.hh and v3's math cannot be mixed in one translation unit"
#endif

#include <cmath>

struct vec2 {
    float x = 0, y = 0;
    constexpr vec2() = default;
    constexpr vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct vec3 {
    float x = 0, y = 0, z = 0;
    constexpr vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    constexpr vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct mat22 { vec2 x, y; };            // rows, like vecmath
struct mat33 { vec3 x, y, z; };
struct mat44 { vec4 x, y, z, w; };

static_assert(sizeof(vec2) == 2 * sizeof(float) && sizeof(vec3) == 3 * sizeof(float)
           && sizeof(vec4) == 4 * sizeof(float) && sizeof(mat44) == 16 * sizeof(float),
              "must stay layout-identical to v3's vecmath types (values cross the C ABI)");

constexpr vec3 operator+(vec3 a, vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr vec3 operator-(vec3 a, vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr vec3 operator*(vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr vec3 operator*(float s, vec3 a) { return a * s; }

// the handful of helpers the C code takes from v3's api.h, same names and meaning
constexpr vec3  lerp3(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
constexpr float dot3(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
constexpr vec3  cross3(vec3 a, vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float    len3(vec3 a) { return std::sqrt(dot3(a, a)); }
inline vec3     norm3(vec3 a) { float l = len3(a); return l > 0 ? a * (1.f / l) : vec3{}; }

namespace mare::core {
using ::vec2; using ::vec3; using ::vec4;
using ::mat22; using ::mat33; using ::mat44;
} // namespace mare::core

#endif
