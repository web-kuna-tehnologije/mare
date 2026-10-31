# Design: world LOD management

Status: **steps 3-7 and 10 done** (`gfx/lod.h` / `lod.c`, the dither field in `inst_t`, the
cross-fade in the mesh and shadow shaders, icon alpha coupled to the mesh fade, the view tiers
and the budget director, icon clustering, per-asset residency in `scene.c`, the dashboard LOD
tab; `asset_test` covers the selector). The "what exists" section documents the state before
step 3; "Streaming and residency" below is the design, of which the per-asset wanted set and
eviction are built and the camera-velocity prediction and per-LOD residency are not (see
KNOWN_ISSUES).

## What exists and where it breaks

Today LOD is three unrelated distance tests:

| Thing | Rule | File |
|---|---|---|
| Authored assets | last LOD whose authored `dist * asset_lod_bias <= d` | `asset.c: asset_pick_lod` |
| Ships | `d < 75` full hull, `< 230` simplified (4 of N ships), beyond that an icon | `scene.c: LOD0_DIST/LOD1_DIST` |
| Towns | `d < 260` full town, beyond that an icon | `scene.c: PORT_DETAIL_DIST` |
| Terrain | per-chunk grid LOD by distance | `terrain.c` |

Three problems follow from that:

1. **It is world-distance based.** The same ship at the same distance covers four times the
   pixels at 4K that it does at 1080p, and the camera's field of view and the map-view flatten
   change the projection without changing the LOD. Detail is budgeted in the wrong unit.
2. **It pops.** Selection is a hard threshold with no hysteresis and no blend: an object sitting
   at 230.0 units flickers between two meshes as the camera breathes, and crossing 260 makes a
   whole town vanish and an icon appear in one frame.
3. **Nothing is budgeted or streamed.** Every LOD of every registered asset is loaded and
   uploaded at startup and kept forever; a zoom out to the continent still walks every port.

## Metric: projected screen size

One number drives every decision: how many pixels the object covers.

```
px = 2 * r * (screen_h / 2) / (d * tan(fov/2))  ->  px = r * k / d,   k = screen_h / tan(fov/2)
```

`r` is the instance's world-space bounding-sphere radius (`asset_t.radius * instance scale`, or a
per-kind radius for the procedural ships and town pieces), `d` the camera distance. In map view
the projection flattens, so `k` is blended with the orthographic equivalent by `cam.mapness`.

Authored `dist` values stay the authoring unit - an artist thinks "swap at 40 m", not "at 18
pixels". They are converted **once at load** into pixel thresholds against a reference
projection (1080p, the game's default FOV):

```
px_threshold[i] = r * k_ref / lod_dist[i]
```

Selection then compares the current `px` against those thresholds, which makes LOD independent
of resolution, FOV and zoom behaviour. `asset_lod_bias` scales the thresholds exactly as it
scales distances today, so the dashboard knob and `MARE_LOD_BIAS` keep working.

## Stability: hysteresis and cross-fade

Per-instance LOD state lives in `gfx/lod.c`, keyed by a stable `lod_key` (kind + stable id:
port index, fleet id, `(port, item)` for town pieces) in a fixed-size open-addressing table with
last-seen-frame eviction. It holds `{ current lod, target lod, fade, last_frame }`.

* **Hysteresis.** Drop to the coarser LOD at `px < thr * (1 - H)`, return at `px > thr * (1 + H)`,
  `H ≈ 0.12`. A camera that hovers on a threshold stays where it is.
* **Cross-fade.** When the target changes, both LODs are drawn for ~0.25 s with complementary
  dithered alpha: a hashed screen-space threshold per fragment, discarded above the instance's
  fade value. No sorting, works with depth writes, and costs one float per instance. `inst_t`
  grows from 12 to 16 floats (`fade` plus three reserved); the mesh, depth and smoke shaders all
  read it.
* **Shadows** draw only the dominant LOD (`fade >= 0.5`), so a cross-fading object does not
  double-darken its own shadow.
* **Mesh to icon** is the same mechanism: the icon's alpha is `1 - mesh fade`, so a town dissolves
  into its marker instead of blinking.

A `lod.pops` metric counts instances whose LOD changed by more than one level in one frame - the
number that should stay at zero while flying across the map.

## View tiers

Five tiers formalise the thresholds that already exist, rather than inventing new ones. The tier
is derived from `cam.dist` (and the detail view overrides it):

| Tier | `cam.dist` | Ships | Ports / towns | Authored assets | Hinterland |
|---|---|---|---|---|---|
| `TIER_CITY` (tactical) | `< 16` (`CAM_CITY_DIST`) | full hull, every ship | full town, props | LOD 0-1 | sites visible |
| `TIER_BATTLE` | `16 - 75` (`LOD0_DIST`) | full hull, every ship | full town | LOD 0-1 | sites |
| `TIER_REGIONAL` | `75 - 260` (`PORT_DETAIL_DIST`) | simplified, 4 of N (battles promoted) | full town, coarse LODs | LOD 1-3 | icons |
| `TIER_STRATEGIC` | `260 - 480` (`CAM_MAP_FULL`) | icon | icon + harbour silhouette | not drawn | icons |
| `TIER_CONTINENTAL` | `> 480` | clustered icon | clustered icon, size by settlement size | not drawn | region shading |

Each tier carries a **budget**: maximum LOD-0 instances, maximum authored-asset instances, maximum
town pieces, and an upload budget in bytes per frame. When a frame exceeds a budget the director
demotes the instances with the smallest `px` by one level (and counts the demotions) instead of
dropping them - quality degrades at the horizon, never in the middle of the screen. The existing
`BATTLE_DETAIL_MAX` ship promotion becomes the first client of this mechanism.

## Streaming and residency

Each `(asset, lod)` pair is in one of `NOT_RESIDENT / REQUESTED / CPU_READY / GPU_RESIDENT`.

* Every frame the director computes the **wanted set** from the current tier and from a
  prediction: the camera's target and distance extrapolated by ~0.5 s of its current velocity, so
  a zoom requests what it is about to need, not what it needs now.
* Missing entries are requested through the job system (`IO -> DECODE`, see `DESIGN_JOBS.md`).
  Ready entries are uploaded on the render thread inside the frame's upload budget.
* Entries not wanted for N seconds are evicted when the VRAM budget is exceeded, coarsest-first
  and never the LOD currently on screen.
* **There is never a hole.** If the wanted LOD is not resident, the nearest resident coarser LOD
  draws; if none is resident, the icon draws. Streaming latency costs sharpness, never objects.
* **Cloud cover.** A zoom that crosses two tiers faster than the streamer can follow (or an entry
  into the detail view) pulls the existing cloud overlay from `staged_scene.cc` - extracted into
  `gfx/clouds.c` so both callers share it - over the frame until the wanted set is resident or a
  maximum time elapses. Fog of war over unexplored regions uses the same overlay path.

## Icons for distant objects

`icons.c` already draws SDF markers at world positions. The LOD work extends it:

* alpha coupled to the mesh fade (above), so the swap is a dissolve;
* size and shape driven by importance (settlement size, fleet strength) rather than a flat
  per-kind constant, so a continental view reads as a map of cities rather than equal dots;
* **clustering** at `TIER_CONTINENTAL`: markers closer than ~12 px merge into one marker with a
  count, computed on the worker lane from the same screen-space metric. This is what keeps the
  fully zoomed-out view legible and cheap once the world has hundreds of settlements.

## As built (steps 3-4)

* `lod_px(r, d) = 2 r k / d` with `k = screen_h / (2 tan(fov/2))`; `lod_px_ref` is the same for the
  1080p / 45 degree reference view. Descriptors may now give a LOD's switch point either as
  `dist` (converted with `lod_px_ref` and the asset's radius) or directly as `px` on the
  reference screen; the generated building descriptors use `px` (220 / 90 / 30) because the
  same world distance means very different pixel sizes for a hut and a church.
* Ships: `[full, simplified, icon]` at 80 / 26 px of fleet footprint; towns `[town, icon]` at
  35 px of town footprint - the pixel equivalents of the old 75 / 230 / 260 unit rules for a
  lone sloop and a size-2 town, so the defaults look the same at 1080p and larger things stay
  3D further out.
* Per-instance state: 16k-entry open-addressing table, keys `(kind, id, sub)`, entries unseen for
  ~2 s are recycled. Hysteresis +-12 %, fade 0.3 s, reversal mid-fade plays the fade back
  (swapping the two levels) instead of snapping.
* Cross-fade: `inst_t.dither` (+t for the level fading out, -t for the one fading in) against an
  ordered 4x4 Bayer threshold in `GLSL_DITHER`, applied by the mesh pass and the shadow pass
  alike, so the two levels partition the pixels exactly and shadows never double-darken. A town
  dissolving into its icon reuses the same field for every piece; the icon pass draws with the
  complementary weight.
* `lod.pops` (level jumps of 2+ in one frame) and `lod.fading` are metrics. The scripted zoom
  (`MARE_ZOOM=6,900,12,-6.0,37.3`: Seville, 6 to 900 units and back in 24 s, 9 authored assets,
  Intel iGPU) recorded 0 pops, up to 83 instances mid-fade and up to 483 authored instances;
  frame time median 4.9 ms, worst 37 ms at the near end where ~250 houses sit at LOD 0/1 -
  the triangle load the step-5 budget is for, not a stall (`frame.cpu` stays under 1 ms).

## Verification

* `asset_test` covers the threshold conversion, hysteresis and budget demotion as pure functions
  (no GL, headless).
* `MARE_CAM` + `MARE_SCREENSHOT` pin a camera at each tier boundary, so the LOD split at a
  boundary is a comparable screenshot per change, as in the earlier renderer steps.
* A scripted zoom from `CAM_MIN_DIST` to `CAM_MAX_DIST` with `--dev` records `lod.pops`,
  `jobs.main_ms` and `frame.dt_ms` to the metrics CSV: the pass criterion is no frame over 2x the
  median and `lod.pops == 0`. `lod.pops` counts multi-level jumps the one-step clamp held back,
  so a non-zero value is a camera teleport (the first frame of a `MARE_ZOOM` whose start
  distance differs from the camera's), never a visible pop; `mare.c` logs it at DEBUG with the
  camera distance so a run log settles which.
* The dashboard **LOD** tab (step 10) shows the live tier, budget use and director scale, the
  instance counters, and GPU residency per asset and LOD; `tools/metrics_summary.py`
  summarises a capture. The step 5-10 captures are tabulated in DESIGN_JOBS.md.
