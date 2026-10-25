# Proposal: a more detailed map

Status: **proposal** (nothing here is implemented). Written after the city view (0.7.9) made the
terrain's limits visible: at 3-16 world units the ground under a town is a handful of bilinear
heightmap texels, the coastline is a staircase of 1/8-degree cells, and beaches are a blur.

## Where the detail ceiling is today

One global heightmap drives everything: `LM_W x LM_H = 1408 x 1040` cells at `CELLS_PER_DEG = 8`
(1 cell = 1/8 degree = 0.5 world units). The renderer (terrain.c) draws 374 chunks with distance
LODs off this grid; the simulation navigates on `NAV_DIV`-coarsened cells; the sea reads the same
map for depth. Raising `CELLS_PER_DEG` globally is the wrong move: 4x the resolution is 16x the
memory (~23 MB floats becomes ~370 MB), the nav grid and region BFS costs blow up with it, and
99% of the new texels would never be looked at up close.

## Proposal: keep the world coarse, synthesize detail where the camera is

Three independent, ordered steps - each one ships value on its own.

**1. Procedural detail octaves in the terrain shader (cheap, biggest win).**
The terrain fragment shader already samples a heightmap texture; add 2-3 octaves of tileable
value noise (the `detail.c` generator already builds such textures) whose amplitude fades in
below ~40 units of camera distance and scales with local slope. Displace the *shading* only
(normal perturbation + albedo breakup: grass tufts, rock exposure, sand ripples), not the
geometry: the simulation, picking and ship draft stay untouched. This is a shader-only change.

**2. A coastline signed-distance field (fixes the staircase).**
Precompute once at init (or bake to disk) a low-res SDF of the land mask (one byte per cell,
same 1408x1040 grid, ~1.4 MB). The terrain and sea shaders then cut the land/water boundary by
the *interpolated* SDF zero crossing instead of the cell grid: coasts become smooth curves at
every zoom, beaches get a stable "distance to water" to drive sand width and surf, and towns can
place piers exactly on the waterline. The land mask used by the simulation stays cell-based, so
nav and region logic do not change.

**3. Camera-centred geometry clipmap for the city view (only if 1+2 are not enough).**
A small (e.g. 128x128) vertex grid that follows the camera target, re-tessellating the existing
heightmap plus the procedural octaves from step 1 as real displacement, blended out at its edge
into the ordinary chunks. This is the only step that adds geometry density, and it is bounded:
one grid, only near the camera, no new global storage. Prerequisite: step 1's noise must be the
same function on CPU and GPU so house placement (scene.c `ground_range`) agrees with what is
drawn.

## Explicitly rejected

- **Raising CELLS_PER_DEG globally** - memory and nav cost scale quadratically for detail that
  is almost never on screen; it also invalidates every saved game's derived data.
- **Authored high-res terrain tiles** - the art pipeline (Meshy + Blender) is building ships and
  towns; hand-painting hundreds of coastal tiles is not a good use of that budget, and the
  procedural steps above keep the whole map consistent.
- **Displacing geometry from the detail octaves globally** - sim/render divergence: ships would
  visibly float or clip where the simulation thinks the water line is.

## Order of work and checkpoints

1. Shader octaves (step 1), behind a dashboard toggle; screenshot pair at dist 6 for review.
2. Coast SDF (step 2); verify piers/surf at ten ports; check init time (< 100 ms target).
3. Decide on the clipmap only after seeing 1+2 in the city view.
