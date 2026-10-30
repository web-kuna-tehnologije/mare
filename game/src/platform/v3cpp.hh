// The v3 framework as seen from C++ (docs/REWRITE_PLAN.md): every converted game TU includes
// this instead of v3.h. The framework itself stays one C translation unit
// (src/platform/v3_impl.c); these are declarations only, forced to C linkage so the C++ game
// links against the C-built framework.
//
// Three vendored headers need special handling under a blanket extern "C":
//  * vecmath is C++-aware (operator overloads in namespace vecmath - overloads cannot carry C
//    linkage), so it compiles first, outside the wrap, with the same vec3->vec3_ renames the
//    framework's own api.h applies. Its functions are all static inline: no linkage to match.
//  * sokol (audio/gfx/spine) declares C++ reference-overloads and stb_ds C++ templates. The
//    game code calls none of them, so ext/ext.h skips those entries under
//    MARE_SKIP_CPP_UNSAFE (the framework C TU still compiles them for itself).
// V3_API becomes extern "C" explicitly, which also covers the declarations that appear before
// the wrap would.

#ifndef MARE_PLATFORM_V3CPP_HH
#define MARE_PLATFORM_V3CPP_HH

#ifndef __cplusplus
#error v3cpp.hh is the C++ face of the framework; C translation units include v3.h
#endif

#ifdef MARE_CORE_MATH_HH
#error core/math.hh and the v3 math types cannot be mixed in one translation unit
#endif

#define V3_API extern "C"

// vecmath, C++ branch, outside the linkage wrap (see header comment)
#define vec2 vec2_
#define vec3 vec3_
#define vec4 vec4_
#define mat22 mat22_
#define mat33 mat33_
#define mat44 mat44_
#include "ext/ext-3rd/ext-0/ext-3rd/ext-vecmath/3rd/vecmath.h"
#undef vec2
#undef vec3
#undef vec4
#undef mat22
#undef mat33
#undef mat44

// Vendored headers with C++ overloads/templates the game never calls (sokol audio/gfx,
// spine, stb_ds): ext/ext.h drops them under this macro
#define MARE_SKIP_CPP_UNSAFE 1

extern "C" {
#include "v3.h"
}

// Framework symbols defined only in implementation headers (the unity build saw their
// definitions; declaration-only TUs need them spelled out):
extern "C" void dd_flush(mat44 proj, mat44 view);

// The dd_* debug-draw macros designate bitfields inside anonymous unions - something C++20
// designated initializers cannot express - so C++ callers build the same defaults
// imperatively and call the plain functions (the macros are retired for C++).
#undef dd_flags
#undef dd_line
#undef dd_point
#undef dd_camera
#undef dd_grid
#undef dd_axis
#undef dd_sphere
#undef dd_plane
#undef dd_frustum
#undef dd_triangle
#undef dd_arrow
#undef dd_normal
#undef dd_circle
#undef dd_prism
#undef dd_text
#undef dd_circle__with_orientation
#undef dd_aabb
struct dd_opts : dd_flags {
    dd_opts() : dd_flags{} { aa = 1; count = 1; n = vec3{0, 1, 0}; color = 7; }
    dd_opts &with_color(unsigned c)  { color = c; return *this; }
    dd_opts &with_top(int v)         { top = v & 1; return *this; }
    dd_opts &with_dashed(int v)      { dashed = v & 1; return *this; }
    dd_opts &with_scale(float v)     { scale = v; return *this; }
};

#endif
