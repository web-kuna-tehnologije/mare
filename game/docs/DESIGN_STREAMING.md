# World streaming: hierarchy, disk tiles, progressive detail, parallel ticks

The world so far is monolithic: one 1408x1040 heightmap generated at startup, every asset LOD
decoded in one piece, every tick phase on the main thread. This design makes the world a
hierarchy of streamable tiles — continent -> region -> coastal tile -> tactical chunk — that
load from disk on demand, live under a memory budget with an explicit unloading policy, and
feed a progressive detail path (icon -> LOD4 -> ... -> LOD0 for cities, a terrain detail
window for the ground). Everything that computes — tile generation, terrain IO, the tick
phases (population, trade, production, movement) — goes through the job system
(DESIGN_JOBS.md), so none of it can stall a frame.

## 1. The terrain grid (real-world map)

World extent (core/types.h): lon **-130 .. 46** (176 deg), lat **-58 .. 72** (130 deg),
base heightmap **1408 x 1040** cells at **8 cells/deg** (1 world unit = 0.25 deg).

Four levels, each a 2x2 split of the one above (a quadtree with the tactical chunk as leaf).
Every tile carries a 257x257 height payload over its extent, so the sample density doubles
per level; the payload is a *delta* against the base heightmap, which is what keeps a missing
tile safe (delta 0 = today's terrain).

| level | name           | tile size (cells) | tile size (deg) | grid    | tiles | samples/deg | payload |
|-------|----------------|-------------------|-----------------|---------|-------|-------------|---------|
| L0    | continent      | 512 x 512         | 64 x 64         | 3 x 3   | 9     | 4           | 264 KB  |
| L1    | region         | 256 x 256         | 32 x 32         | 6 x 5   | 30    | 8 (= base)  | 264 KB  |
| L2    | coastal tile   | 128 x 128         | 16 x 16         | 11 x 9  | 99    | 16 (2x)     | 264 KB  |
| L3    | tactical chunk | 64 x 64           | 8 x 8           | 22 x 17 | 374   | 32 (4x)     | 264 KB  |

512 tiles total, ~135 MB if everything were resident at once — which is exactly what the
budget (section 3) prevents. The L3 tactical chunk matches the renderer's existing 64x64-cell
terrain chunk (gfx/terrain.c `CHUNK`), so the city/battle view and the streamer agree on
what "near the camera" means. Tiles at the east/south edge clamp to the world extent.

### L0 continents (3 x 3, named left-to-right, top-to-bottom)

| cell | lon range   | lat range  | name |
|------|-------------|------------|------|
| 0,0  | -130 .. -66 |   8 .. 72  | North America & Arctic |
| 1,0  |  -66 ..  -2 |   8 .. 72  | North Atlantic & Western Europe |
| 2,0  |   -2 ..  46 |   8 .. 72  | Europe, Mediterranean & Levant |
| 0,1  | -130 .. -66 | -56 ..  8  | Pacific Coast & New Spain |
| 1,1  |  -66 ..  -2 | -56 ..  8  | South America & South Atlantic |
| 2,1  |   -2 ..  46 | -56 ..  8  | Africa & Indian Approaches |
| 0..2,2 | (all)     | -58 .. -56 | Southern Ocean margin (water sliver) |

### L1 regions (6 x 5)

Rows top (lat 40..72) to bottom; columns west to east at lon -130, -98, -66, -34, -2, 30(..46).

| row \ col | 0 (-130..-98) | 1 (-98..-66) | 2 (-66..-34) | 3 (-34..-2) | 4 (-2..30) | 5 (30..46) |
|-----------|---------------|--------------|--------------|-------------|------------|------------|
| 0 (40..72)   | Hudson Bay & Shield | Great Lakes & Labrador | Newfoundland & N Atlantic | British Isles & Norway | Baltic & Central Europe | Muscovy & Black Sea N |
| 1 (8..40)    | New Spain Pacific | Gulf of Mexico & Florida | Caribbean & Sargasso | Azores & Canaries | Mediterranean & Sahara | Levant & Egypt |
| 2 (-24..8)   | Eastern Pacific | Peru & New Granada | Brazil & Amazon | Mid-Atlantic | Guinea & Congo coast | East African rim |
| 3 (-56..-24) | South Pacific | Chile & W Patagonia | La Plata & E Patagonia | South Atlantic | Cape of Good Hope | SW Indian Ocean |
| 4 (-58..-56) | Southern Ocean sliver (x6, water) | | | | | |

L2 coastal tiles and L3 tactical chunks are addressed numerically (`L2 x,y` / `L3 x,y`); the
debug overlay and the dashboard print the owning L1 name next to the coordinates. Widening
the world for Asia later only grows the grids; the addressing scheme is unchanged.

## 2. Streaming architecture (world/stream.h/.c)

A tile is `(level, tx, ty)` with states **EMPTY -> QUEUED -> READY** (+ `on_disk`):

- **Wanted set**: once per frame, `stream_update(cx, cz, dist)` descends the quadtree from L0.
  A node splits while the camera is within `STREAM_SPLIT_K` times its world size, so a zoomed
  out view wants a handful of L0/L1 tiles and a city view wants its L3 chunk and neighbours —
  the classic quadtree/clipmap wanted set. Wanted tiles that are EMPTY are queued.
- **Loading**: queued tiles first try the disk cache (`tiles/L<level>_<tx>_<ty>.mht`, read on
  the **IO lane**); on a miss they are generated on the **DECODE lane** (low priority: a burst
  of tile builds can never starve the frame-critical WORKER lane) — deterministic fbm detail
  octaves beyond the base map's resolution, seeded by `(world seed, level, tx, ty)`, faded out
  below `h < 0.5` and over water so coasts, harbours and town ground stay exactly where the
  simulation put them — then written back to disk on the IO lane. The commit of a finished
  payload runs on the **MAIN lane**, so tile state only ever changes on the render thread (the
  same discipline as asset.c). Terrain data loading therefore uses the identical worker-pool
  path as everything else.
- **The simulation never reads tiles.** `world_height`/`world_is_land` stay pure functions of
  the base heightmap; tile deltas are presentation only. Residency is camera-driven and must
  never influence a deterministic tick.

### 2b. Two clipmap rings and their lighting (follow-ups 2 and 3)

The detail is carried by **two** windows around the camera, not one:

| ring | extent | texel | samples | level it may carry |
|------|--------|-------|---------|--------------------|
| near | 80 units | 0.156 | 512² | L3 tactical (6 cycles/deg) |
| far  | 320 units | 0.625 | 512² | L2 coastal (3 cycles/deg) |

Each extent is set by Nyquist against the level's detail frequency: a ring may only carry
detail it can sample twice per wavelength, which is why the far ring asks
`stream_delta_at_level(..., STREAM_L_COASTAL)` instead of the finest resident tile. The near
ring cross-fades into the far one at its border (not into nothing), so pulling the camera back
loses resolution rather than relief. At most one ring is recomposed per frame, the near one
first.

The terrain shader differentiates the composed delta in world space
(`det_at`/`det_step`, shared by the vertex and fragment stages) and adds that gradient to the
base map's before normalising: streamed relief is **lit**, where before it was displaced
geometry shaded by the base map's normals and read as flat painted ground.

## 3. Memory budget and unloading policy

- Budget: `STREAM_BUDGET_MB` (default **64 MB**, `MARE_STREAM_MB` overrides) of resident tile
  payloads — a quarter of the whole set, far more than a wanted set ever needs (~20 tiles).
- Policy, applied in `stream_update` after the wanted set is computed:
  1. a tile in the current wanted set is never evicted;
  2. a tile whose children are resident is kept (the coarser fallback that makes zooming out
     instant and a stream-in hole impossible);
  3. over budget, the least-recently-wanted tile goes first (LRU by `last_want` frame),
     at most a few per frame — eviction is never a stall;
  4. eviction frees the payload only; the disk cache keeps the file, so coming back is one
     IO-lane read, not a regeneration.

## 4. Progressive city loading: icon -> LOD4 -> ... -> LOD0

Today an asset flips LOADED with all five LODs decoded, and the *picked* LOD uploads
synchronously on first draw. Progressive loading inverts the order:

- the commit queues GPU uploads **coarsest first** (LOD4 .. LOD0), one budgeted MAIN job each;
- the scene draws the **nearest resident LOD at or coarser than the pick** instead of forcing
  the pick onto the GPU mid-frame; while nothing is resident the town keeps its icon (or its
  procedural fallback), so a city fades in as icon -> LOD4 -> LOD3 -> ... -> LOD0, each step
  through the existing cross-fade (lod.c) — never a pop, never a frame-long upload stall;
- `asset.lod_fallbacks` counts instances drawn coarser than picked (it must fall to 0 within
  moments of the camera settling).

## 5. Tick phases on the worker pool

Every per-entity tick phase splits into a **parallel compute** over entities (pure function of
the pre-tick state, writes only the entity's own fields) and an **ordered sequential commit**
(treasury, the chronicle, the shared rng, cross-entity events), in entity order. Determinism
holds because `jobs_parallel_for` chunking is schedule-independent and every rng call stays on
the main thread in the same order (sim_test's bit-exact save/load check covers it, as its unity
build links the real pool):

- **economy** (ports: production, consumption, prices, population growth): parallel per-port
  compute, deferred tax/log commit. Bit-identical to the sequential order.
- **settlements** (immigration, assimilation, unrest): parallel prepare (including the
  O(ports x fleets) immigration-source weights), sequential commit for the rng picks,
  secession and labour drift.
- **regions** (rural production, hub links, inland settlements): parallel per-region flows
  (hub port sets are disjoint), deferred tax/log; the once-a-day development pass stays
  sequential.
- **fleets** (movement): a parallel pre-pass computes each sailing fleet's wind factor and
  effective speed (the expensive trig); the sequential tick consumes it (keyed on exactly the
  inputs the move would use, so an AI re-path recomputes inline, bit-identically) and keeps
  combat, trade, arrival and AI thinking in order. Battle resolution and colonization are
  rng-driven and stay sequential by design.
- **The dispatch is adaptive.** Measured with `sim_test 1` on the Windows pool: 17.7 s with the
  phases sequential, 54.6 s with every phase forced onto the pool - waking the workers costs
  ~2 ms per `parallel_for`, a whole 192-port economy step ~0.2 ms. A phase therefore dispatches
  only once its batch reaches `PARSIM_MIN_BATCH` (2048) entities, which the full-world port and
  fleet counts this design aims at will reach and today's world does not. The compute/commit
  split is the permanent value: the tick is *ready* to scale. `MARE_PARSIM=0` never dispatches,
  `=2` always (benchmarking), unset = adaptive.
- **regions** stay sequential on purpose: 30 regions x 17 goods is microseconds, and their hub
  taxes interleave with port stocks in an order worth keeping simple.

## 6. Sea height follows the zoom

Wave displacement now scales with camera distance: full height up close (city/battle view),
flattening as the view pulls out (`sea_zoom_amp`), reaching the doldrums look well before the
map morph. Ships ride the same scaled field (meshes.c binds the same amplitude), and the
camera's wave-crest floor uses it too. Tessellation is capped lower (`sea_tess_level_max` 32,
~11 px cells): zooming in raises wave height back to full, not the subdivision count.

## 7. Streaming debug metrics

Dashboard **Streaming** tab + CSV metrics: `stream.resident`, `stream.wanted`,
`stream.loading`, `stream.mb` (vs budget), `stream.evicted`, `stream.disk_hits`,
`stream.generated`, `stream.gen_ms` / `stream.io_ms` (last tile), `asset.lod_fallbacks`.
Debug overlay (dashboard toggle "tiles"): every tile of the wanted set drawn as a frame at its
world bounds — green resident, yellow loading, red missing, blue = evicted but on disk —
with its address and, for tiles that have no authored terrain mesh yet, a flat placeholder
quad: the list of grids still to model in Blender, in place, in the world.

## 8. Authored terrain tiles (Blender)

A tactical chunk or coastal tile can be replaced by an authored mesh: export it as an asset
named `tile_l<level>_<tx>_<ty>` (type `terrain`, see docs/BLENDER_LOD.md for the exporter
workflow). When the manifest holds such an asset the streamer marks the tile "authored", the
debug overlay stops flagging it, and the scene draws the mesh at the tile's world origin
through the normal asset path (LODs and all). Tiles without one keep the procedural detail.

## Task list

1. [x] This design: hierarchy, the terrain grid listing, budgets, policy, task list.
2. [x] `world/stream.h/.c`: quadtree, tile states, disk cache, generation on the pool,
       memory budget + LRU unloading, metrics; headless `tests/stream_test.c` (determinism,
       disk round-trip, budget, eviction, coast fade); build scripts + CMake.
3. [x] Terrain detail window: compose the finest resident tiles into one R32F detail texture
       around the camera; blend it in the terrain shader; tile debug overlay with states and
       placeholder frames for unauthored tiles; authored `tile_l*` asset hook.
4. [x] Progressive city loading: coarse-first uploads, resident-LOD clamp with coarser
       fallback in the scene, `asset.lod_fallbacks` metric.
5. [x] Tick phases on the worker pool (economy, settlements, regions, fleet speed pre-pass),
       `MARE_PARSIM`, sim_test still bit-deterministic.
6. [x] Sea: zoom-scaled wave height, lower tessellation ceiling.
7. [x] Dashboard Streaming tab + metrics.
8. [x] Docs: CHANGELOG, ROADMAP, FEATURES, BLENDER_LOD.md (exporter how-to).
