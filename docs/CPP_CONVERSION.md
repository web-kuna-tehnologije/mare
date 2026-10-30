# C → C++ module conversion (rewrite tasks T4–T7, docs/REWRITE_PLAN.md)

Every game module becomes a C++20 translation unit holding one class; the existing C headers
stay the inter-module contract. `game/src/sim/combat.cc` is the exemplar — copy its shape.

## The recipe, per module

1. **Rename** `x.c` → `x.cc`. Keep the file where it is. Delete the old `.c` (git mv is fine).
2. **Includes**: own header plus the other *game* headers it used, all inside one
   `extern "C" { ... }` block (every game header carries its own `#ifdef __cplusplus` guard —
   keep the wrap anyway for the `#include <...>` hygiene and to make intent explicit; system
   headers move above it as `<cstdio>`-style includes).
   A module that touches the v3 framework (GL via ext, `app_*`, `dd_*`, `mu_*`, GLFW keys,
   `time_ss`, `fps`, stb declarations…) includes `"../platform/v3cpp.hh"` **first** instead —
   never `v3.h` directly, never `core/math.hh` in the same TU.
3. **Class**: wrap the implementation in the module's namespace and class.
   - Namespaces: `mare::game::sim` (sim/), `mare::game::world` (world/), `mare::game::view`
     (gfx/scene, gfx/shipyard, gfx/detailview), `mare::engine::assets` (assets/),
     `mare::engine::render` (rest of gfx/), `mare::ui` (ui/), `mare::game` (core/log.c).
   - File-scope `static` *state* → private members (same names; initializers become default
     member initializers). File-scope `static` functions → methods (private unless the header
     exposes them). Public API functions → public methods (drop the module prefix:
     `combat_tick` → `tick`).
   - A function-local `static` stays a function-local static.
   - Singleton accessor: `Module &module_name() { static Module m; return m; }` in the
     namespace. No globals of class type at namespace scope (init order).
4. **C ABI**: at the bottom, one `extern "C"` block implements every function the header
   declares as a one-line shim over the accessor. Shared *globals* the header exposes
   (`RF`, `cam`, `G`, `sea_wave_amp`, …) stay plain globals **defined in the same .cc**,
   outside the namespace (their declarations in the headers are already `extern "C"`).
5. **Logic is untouched.** This is a port, not a refactor: same statements, same comments,
   same formatting quirks. Only the containers around the code change. Keep every comment.

## C → C++ fixes you will hit

- **Compound literals** `(T){ a, b }` → `T{ a, b }`; `(T){ .x = a }` → `T{ .x = a }`
  (designators must stay in declaration order; reorder members in the *initializer*, never in
  the struct). `memset(&t, 0, sizeof t)` and `= {0}` stay fine.
- **void\* conversions**: `malloc`/`calloc`/`realloc` results and `t->data`-style reads need
  explicit casts: `(float *)malloc(...)`.
- **Implicit enum ↔ int**: add casts where MSVC complains (`(job_lane_t)i`).
- **String literals** are `const char*`: a `char *p = "x"` needs `const`.
- **VLAs do not exist**: a `float tmp[n]` with runtime `n` becomes a fixed maximum (look for
  the bound the module already asserts) or a small heap allocation freed in the same scope.
- **`float f[3] = {x}` zero-fill semantics are identical** — leave them.
- Don't introduce STL containers, exceptions, or allocation in per-frame paths; the existing
  fixed arrays stay.

## What not to do

- Do not edit `game/mare.c`, the build scripts, or `game/CMakeLists.txt` (integration is
  centralized). Do not touch other modules' files, shaders, or headers beyond adding a
  missing `extern` declaration for a symbol the unity build shared implicitly (put it in the
  owner's header, inside the extern-C region, with a comment).
- Do not convert the *tests*; they are handled with the build integration.

## Verifying (integrator only)

Mirror to `C:\projects\mare\tmp\elm-cpp` and `cmd.exe /c "cd /d ... && game\build.bat"`;
the .cc files are picked up by the build's glob. Headless suites + a battle screenshot gate
every wave.
