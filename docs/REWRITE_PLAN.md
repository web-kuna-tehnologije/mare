# C++ rewrite plan (started 2026-10-30, worktree `elm`)

Goal: move the game from C11 (unity build) to C++20 with real classes, namespaces, SOLID/DRY,
an entity lifecycle, shaders on disk, an abstract render device, and finally a Vulkan backend —
while the game builds and the five test suites stay green after every task.

## Ground rules

- The **v3 framework and `ext/` stay C** (third-party, tens of thousands of lines). They compile
  as their own C translation unit behind a C ABI, exactly as `jobs.cpp` was the lone C++ TU
  before — the relationship just inverts as modules port over.
- **Strangler pattern**: each ported module leaves the unity build (`game/mare.c`) and becomes a
  C++20 translation unit. Unported C callers keep working through thin `extern "C"` shims that
  delegate to the new classes. No big-bang switch; builds stay green per task.
- **Determinism is sacred**: the simulation's bit-identical parallel_for reductions
  (docs in game/docs/DESIGN_JOBS.md) must survive every port. Parallel entity updates are
  opt-in, never default.
- Namespaces: `mare::core` (diag, worker pool, math), `mare::engine` (entities, assets,
  rhi, render), `mare::game` (sim, world, chronicle), `mare::ui`. Physical folders follow the
  namespaces as modules port (src/core, src/engine, src/game, src/ui, src/platform).

## Task list

- [x] **T1 — Foundation: cleanup + `mare::core`.** Split the unity build (framework C TU +
      game C++ TUs); port diag.c → `core::Logger`/`core::Metrics`/`core::Profiler` behind a
      `Diagnostics` facade with pluggable sinks (SRP/OCP); refactor jobs.cpp into
      `core::WorkerPool` (RAII, lanes, parallel_for) — C ABI of diag.h/jobs.h preserved for
      unported code; tests link the C++ objects instead of including .c files; build scripts
      and CMake updated; MSVC mirror build + all ctest suites green.
- [x] **T2 — Entity lifecycle (`mare::engine`).** `engine::Entity` base class with virtual
      `onLoad / onLoadAsync / onUpdate / onRender / onUnload`; `engine::EntityManager` owns
      entities, runs `onLoadAsync` on the WorkerPool lanes, dispatches `onUpdate` through
      `jobs_parallel_for` for entities that declare `UpdatePolicy::Parallel` (serial default:
      determinism), `onRender` always on the GL thread. First real entities ported
      (wind, weather) and the main loop drives the manager.
- [x] **T3 — Shaders to files.** Every embedded GLSL string (core macros in gfx/glsl.h and the
      ~20 shaders inline in gfx/*.c) moves to `game/assets/shaders/**` as .vert/.frag/.tesc/
      .tese files with `#include` for the shared chunks; `engine::ShaderLibrary` (C++) loads,
      preprocesses and caches them, with a C shim `shader_source()` for unported pipelines and
      a dev-mode missing-file error that names the file. DRY: the fog/color/wave chunks exist
      exactly once.
- [ ] **T4 — Render device abstraction (`mare::engine::rhi`).** rhi.h/rhi_gl.c become an
      abstract `rhi::Device` interface + `rhi::GlDevice`; backend chosen at startup
      (`--rhi=gl|vulkan|dx12`); the C `rhi_*` functions become shims over the active device so
      unported gfx code is agnostic. This is the seam T8 plugs into.
- [ ] **T5 — gfx port (`engine::render` + `game::view`).** terrain/sea/sky/hdr/lighting/
      meshes/lod/occlusion/textures/streamview → classes (Entities where the lifecycle fits);
      shipyard/scene/detailview → `game::view`. Shared fullscreen-pass and mesh-builder helpers
      deduplicated.
- [ ] **T6 — sim port (`mare::game`).** game/fleet/settlement/region/economy/ai/combat/events
      → classes; inheritance where a real is-a exists (map agents, port vs inland settlements);
      `G` becomes a `game::Game` instance; world/nav/stream/terrain tiles → `game::world`;
      sim_test/stream_test ported and still bit-deterministic.
- [ ] **T7 — UI port (`mare::ui`) + UI technology decision.** Port uikit/hud/dashboard/input to
      classes. Recommendation (see below): no embedded browser; optionally adopt RmlUi behind a
      `ui::Backend` interface if richer layout is wanted.
- [ ] **T8 — Vulkan backend.** `rhi::VulkanDevice` implementing the T4 interface (swapchain,
      baked pipelines, per-pipeline uniform blocks, pass graph); GL stays default until
      screenshot parity; DX12 remains a stub proving the interface is API-neutral.
- [ ] **T9 — Final folder/docs sweep.** Retire what is left of mare.c; src/ tree fully matches
      the namespaces; README/docs/build scripts reflect the C++ layout.
- [ ] **T10 — Known bug fixes** (docs/TODO.md "Still open"): `MARE_PASSES=sea` +
      `MARE_SEA_DEBUG=1` incomplete render target; hard-edged overhead cloud layer and faceted
      close-range gunsmoke; consume or drop the 300 MB normal-map tile cut; retire the
      contested shared `assets/terrain/tiles` folder.

## UI technology recommendation (for T7)

A real HTML engine (CEF / WebView2) is the wrong tool here: 100+ MB of runtime, its own
process/compositor, and input latency that *would* tank the frame. The practical middle ground
is **RmlUi** — an HTML/CSS subset that renders as plain textured triangles through our own RHI
(1–2 ms per frame for a full HUD), styleable in CSS, no browser. The current microui-style kit
is kept for debug panels either way; RmlUi slots in behind a `ui::Backend` interface only if we
want richer layout than the dashboard needs. Decision after T7's port, when the interface
exists and both can be measured.

## Build/verify loop (WSL worktree → MSVC)

The worktree lives on the Linux side with no local compiler; builds run on Windows:
rsync sources to `C:\projects\mare\tmp\elm-cpp` (own mirror — `elm-fin` belongs to the previous
sessions), then `cmake -S game -B build` + `cmake --build --config Release` + ctest via cmd.exe.
