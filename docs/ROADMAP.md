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

## Next: optimisation and multithreading (planned)

The renderer and simulation are single-threaded today. Planned order of work:
1. **Profile first** - the dashboard already splits CPU scopes and GPU passes per frame; pick
   the top three costs at the two worst views (map fully zoomed out, 50-ship detail battle).
2. **Simulation step on a worker thread** - the fixed 0.1-day ticks are independent of
   rendering; double-buffer the game state or hand the renderer a read-only snapshot.
3. **Parallel scene build** - town instance caches, ship instance lists and frustum culling
   are embarrassingly parallel per port/fleet.
4. **Asset loading off the main thread** - GLB parsing + JPEG decode already takes ~800 ms at
   startup for one asset; a loader thread with GPU upload on the main thread removes the stall
   as the asset set grows.
5. **Draw submission** stays single-threaded (GL); batching and instance-buffer reuse already
   keep it cheap.
