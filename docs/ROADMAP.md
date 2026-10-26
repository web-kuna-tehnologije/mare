# Development roadmap

## Done: renderer modernisation (game/docs/DESIGN_RENDERER.md)

All ten steps are complete: RHI abstraction, HDR, linear/sRGB, PBR, directional lighting,
cascaded shadows, normal maps, frustum culling, instancing and CPU/GPU/VRAM/RAM profiling.
Possible follow-ups: a Vulkan or DX12 backend behind the RHI, terrain that casts shadows from a
dedicated caster mesh, ship wakes, and a day/night cycle driving the existing sun.

The recovered baseline implements the transcript's 0.7 core. These remaining
ideas are planned features, not claims of completed work.

## Next: hinterlands 0.8

1. Standalone region panel: production, shortages, hub capacity and development.
2. Player rural labour allocation, separate from existing port production controls.
3. Chartered inland settlements with costs, ownership and gameplay feedback.
4. Interior settlements switching hubs when ports merge, fall or change hands.
5. Per-good shortage targeting for AI traders, with reproducible balance scenarios.
6. River links and Market/Docks capacity effects validated by throughput tests.
7. Copper hull upgrades and iron shipyard discounts with explicit resource costs.

## Presentation and long-term foundations

- Smoother region borders and more historical pins.
- Fleet label collision avoidance and framework HiDPI panel input.
- Ship/city art, weather, onboarding and repeatable performance scenes.
- Portable versioned save serialization instead of a compiler-dependent struct dump.
- Balance AI treasury inflation and coordinated naval warfare.
- Expand Africa and Asia with data-driven geography, ports and resource regions.

Before each feature: update its design, implement a playable slice, add meaningful
simulation tests, visually check affected UI, then update FEATURES and CHANGELOG.

## In progress: engine scale 0.8 - job system and world LOD

Two designs drive this work: [DESIGN_JOBS.md](../game/docs/DESIGN_JOBS.md) (multithreading) and
[DESIGN_LOD.md](../game/docs/DESIGN_LOD.md) (LOD management). The goal is no stall during tick
processing, world traversal, zooming or LOD streaming, at every scale from a town square to the
whole Atlantic.

The job system comes first because the LOD streamer is built on it. Each step lands fully
working on all three platforms, with tests and a measurement, before the next starts.

1. [x] **C++ toolchain + job system core** - `core/jobs.h` (C ABI) and `core/jobs.cpp` (C++17,
       the project's first C++ translation unit): worker pool, the WORKER/IO/DECODE/MAIN lanes,
       join counters, `jobs_parallel_for`, the main-thread drain, statistics, `MARE_JOBS`
       synchronous mode. `tests/jobs_test.cpp`; build.sh / build.bat / CMake / CI updated.
2. [x] **Asset loading on the pools** - split `asset_load` into disk read (IO), glTF parse,
       texture decode and mesh processing (DECODE) and GPU upload (MAIN, inside a per-frame
       budget); non-blocking `asset_request` with an explicit state machine; parallel startup
       preload. Removes the ~800 ms per-asset startup stall.
3. [x] **Screen-space LOD selection** - `gfx/lod.h/.c`: projected-pixel metric, authored
       distances converted to pixel thresholds at load, LOD bias, per-instance state with
       hysteresis. Ships, towns and authored assets all select through it.
4. [x] **Stable transitions** - per-instance fade in `inst_t`, dithered cross-fade in the mesh,
       depth and smoke shaders, mesh-to-icon dissolve, a `lod.pops` metric that must stay at 0.
5. [x] **View tiers and the LOD director** - city / battle / regional / strategic / continental
       tiers with per-tier policy and budgets, demotion of the smallest instances when over
       budget (generalises today's `BATTLE_DETAIL_MAX` ship promotion).
6. [x] **Icons and clustering for distant objects** - icon alpha coupled to the mesh fade, size
       and shape by importance, screen-space clustering at continental zoom.
7. [x] **LOD streaming and residency** - wanted-set prediction from camera velocity, request and
       eviction through the job system, VRAM and upload budgets, coarser-LOD fallback so a
       stream-in never leaves a hole, shared cloud cover (`gfx/clouds.c`) for zooms that outrun
       the streamer.
8. [x] **Simulation work on the job system** - the port-distance rows (the only simulation work
       big enough to show in a frame: ~15 ms each, 111 of them) are precomputed in parallel at
       game start instead of landing lazily inside ticks. `sim_test` links the real pool, so its
       save/load determinism check now covers that work. The per-entity phases (economy,
       movement, regions) measure at 1-2 us each and are left sequential on purpose: see
       DESIGN_JOBS.md, "What is not worth parallelising".
9. [ ] **Async save / load** - snapshot and write on the IO lane, autosave without a hitch.
10. [ ] **Observability and evidence** - dashboard Jobs and LOD tabs, lane and queue metrics,
        before/after captures at the two worst views (fully zoomed-out map, 50-ship battle) and
        a scripted full-range zoom; FEATURES, CHANGELOG, KNOWN_ISSUES and ONBOARDING updated.

Draw submission stays single-threaded (the GL context belongs to the render thread); batching
and instance-buffer reuse already keep it cheap.

Landed alongside steps 5-8 (0.8.3): 3D Gerstner waves the ships ride, the authored sea in the
staged scenes, a day/night cycle driving the shadows, and a bigger battle framing.

Landed alongside steps 2-4 (0.8.2): the authored sea tiles (`game/assets/sea`, streamed through
the same lanes), a Blender LOD exporter + descriptor generator for the Meshy building set, and
the first use of those buildings in towns and the staged city. The next step that matters most
on integrated GPUs is 5: the city view with ~150 authored houses is triangle-bound until the
director budgets LOD 0.
