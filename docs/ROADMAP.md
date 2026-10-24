# Development roadmap

## In progress: renderer modernisation (game/docs/DESIGN_RENDERER.md)

Steps 1-9 are done (RHI, HDR, linear/sRGB, PBR, directional lighting, shadows, normal maps,
frustum culling, instancing). Next: step 10, CPU/GPU/VRAM/RAM profiling hooks.

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
