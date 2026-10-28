# Design: visual rendering 0.9

Status: **steps 1-6 done**; their sections are below. The plan below is the feature "visual rendering" broken into
steps that each land fully working on all three platforms, with a measurement, before the next
starts (the same rule the renderer and engine-scale plans followed; those are in
DESIGN_RENDERER.md, DESIGN_JOBS.md and DESIGN_LOD.md). Each step has its own section below once
it is built.

1. [x] **Occlusion culling** - authored buildings hidden behind other buildings are left out
       of the main pass: hardware occlusion queries on their bounding boxes against the depth
       the opaque pass wrote (`gfx/occquery.c`), answers applied a frame later, nothing culled
       while the camera moves. The terrain-horizon ray (`gfx/occlusion.c`) was built and
       measured first and found to hide nothing with this relief and camera; it stays, nearly
       free, for towns and ships. Toggles: dashboard "occlusion", `MARE_OCCQ=0|1|2`,
       `MARE_OCCLUSION=0`.
2. [x] **Anisotropic filtering** - `rhi_init` asks the driver for its maximum (16x on the dev
       box) and `rhi_texture_make` applies it to every mipmapped texture; the dashboard System
       tab cycles it and `MARE_ANISO=N` pins it. The sea and the roofs are seen almost edge-on,
       which is exactly where trilinear filtering picks too blurry a mip.
3. [x] **Cheap shadow casters** - a shadow-map texel covers a good part of a house, so the
       cascades cast from a private copy of each asset's *coarsest* LOD (2k triangles instead
       of 80k) built in the upload hook, with its own instance buffer carrying every instance
       including the occluded ones. The cross-frame static cache the step originally proposed
       is not viable: the day/night cycle swings the sun 5 degrees a second at the default
       speed, so every cascade would be dirty every frame. `MARE_SHADOW_LOD=0` compares.
4. [x] **Wind-driven sea** - the authored four-wave set is gone. `sea_update_waves` builds five
       Gerstner waves each frame from the wind at the look-at point (`wind_at`): directions
       spread around the wind with the spread narrowing as it strengthens, wavelengths a
       geometric series whose longest member grows with it, height scaling from glassy in the
       doldrums to heavy in a westerly, and a per-wave random offset from the game seed. Phase
       is accumulated per wave rather than computed as speed x time, so a wavelength that
       changes with the wind never makes the crests jump. The waves-change-speed-with-the-camera
       artefact is fixed by fading out every wave the detail grid cannot sample at that point
       (`MARE_SEA_NYQUIST=0` shows what it removes).
5. [x] **Tessellated sea with the authored displacement map** - `rhi_pipeline_desc_t` takes
       control/evaluation stages and draws patches (GL 4.0; `rhi_tessellation_supported`). The
       sea's detail region is a 50x50 lattice of quad patches snapped to a world lattice around
       the look-at point, 300 units in radius (a disc; the world grid cuts a matching hole),
       each edge subdivided to ~9 px cells up to 48 per edge. The evaluation stage places the
       heightmap, the wave sum with the cell size for the grid-aware fade, and the authored
       displacement map `game/assets/sea/sea_displacement.png` on top for the chop five waves
       cannot give. The warped grid remains the fallback (`MARE_SEA_TESS=0`). The other two maps
       in `assets/sea` do not tile (`tools/sea_textures.py --seamless` prepares them) and are
       not used yet.
6. [x] **Foam** - whitecaps where the Gerstner Jacobian folds the surface (plus the steepest
       faces), gated by wave height and textured by the foam albedo; a shoaling boost steepens
       waves off the beach and a surf sheet, scrolling shoreward along the depth gradient,
       breaks in that band; bow wave, hull line and a V wake around the 24 ships nearest the
       look-at point (`sea_set_ships`, fed by scene.c and the staged battle), combined as a
       bounded union so a melee does not turn white. `MARE_FOAM` / dashboard knob.
7. [ ] **Calm weather** - when the wind drops (doldrums, the lee of a storm): a glassy sea,
       haze that softens the horizon, mist in the bays at dawn, drizzle; the existing storm
       events get rain and spray. Weather state derived from the wind field and the events,
       exposed to the shaders as one struct.
8. [ ] **Sky: clouds and sun** - a cloud layer (fbm, lit, moving with the wind) that shadows the
       ground through the light pass, a sun disc with a glow where the sun is, the sky gradient
       following the time of day; the detail-view cloud overlay is extracted into `gfx/clouds.c`
       and shared.
9. [ ] **Ambient occlusion** - screen-space ambient occlusion from the HDR pass depth (towns,
       hull/sea contact) plus a terrain horizon term from the heightmap for valleys, both
       applied to the hemispheric ambient only (the sun term keeps the shadow map).

## Step 1: occlusion culling (done)

**What was measured first.** The plan assumed the terrain is the occluder. `tools/occ_probe.c`
(headless: world + game state, no GL) put a camera at every living town from 24 placements
(14 / 40 / 120 units, 12 / 22 / 35 degrees, 8 yaws) and counted what the terrain ray in
`gfx/occlusion.c` hides: **1 town of 37,311 views and 0 of 53,686 land boxes**. The heightmap
tops out at 7 world units, the camera never pitches below 12 degrees, and towns sit on the
coast, so a ray from a mast top climbs out of the terrain band within a sample or two. The
ray test is kept for towns and ships because that early-out makes it cost nothing (218
heightmap reads for the whole probe) and it covers the one case that exists: the camera
pitched down in a valley. The terrain-chunk variant was removed.

**What hides things here is buildings.** In the city and battle tiers a hundred-plus authored
houses (80k triangles each at LOD 0) stand at a 12-28 degree pitch and the front rows cover the
back rows. So each authored instance is tested on the GPU:

* `scene.c: asset_push` asks `occq_request(key, asset, inst)` and applies `occq_visible(key)`;
  the keys are the LOD selector's stable per-instance keys, so an instance keeps its answer
  across frames. A hidden instance is flagged (`inst_t.fx1`) rather than dropped.
* `scene_assets_end` partitions each instance list visible-first before its one upload of the
  frame; the shadow pass draws the whole list (a hidden house still casts a visible shadow),
  the main pass draws the visible prefix. No second upload: the first version re-uploaded a
  compacted list between the two passes, writing into a buffer the shadow pass was still
  reading, and one run of that on the Intel driver spent 400 ms a frame.
* After the opaque meshes, `occq_flush` draws every requested instance's bounding box (grown
  6 % + 3 cm) with colour and depth writes off inside a `GL_ANY_SAMPLES_PASSED` query (the
  conservative variant when the driver takes it). `occq_frame_begin` collects answers the next
  frame; unanswered queries are not reissued, and one unanswered for 90 frames is abandoned.
* Unknown means visible, so does a stale answer (older than 4 frames), and so does a frame in
  which the camera translated more than 5 cm or turned: culling applies to steady views, where
  the triangle load actually hurts, and never shows a hole while panning. Instances with the eye
  inside their box skip the query (the near plane would clip the proxy).
* Active in the city and battle tiers and in the staged detail scenes; off in the map.

**Measured** (Release, Intel integrated graphics, `--dev --skip=120`, 14 s runs, steady camera,
rows after 4 s; `MARE_OCCQ=1` culls, `2` issues the queries but hides nothing, `0` off):

| view | buildings hidden | frame ms median, culling on / queries only / off |
|---|---|---|
| Seville city tier (`MARE_CAM=-6.0,37.3,14,35`) | 104 of 155 | **15.3** / 15.5 / 16.8 |
| Seville street level (`MARE_CAM=-6.0,37.3,9,120`) | 67 of 109 | **13.9** / - / 14.6 |
| staged city (`MARE_DETAIL=city`) | 65 of 151 | **12.4** / - / 13.6 |

Two thirds of the buildings are culled and the frame gains 0.7-1.4 ms: the queries themselves
cost ~0.1 ms, and the remaining time is the shadow pass (8-9 ms of GPU in the city view),
which by design still draws every caster. That is step 3's job; this step removes the main
pass's share. `gfx.occluded_assets`, `gfx.occ_queries`, `gfx.cam_moving` are metrics; the
dashboard Performance tab shows hidden / queried / pending / tracked and the camera-moving
state, the LOD tab the instance counts.

## Steps 2-4 as built

### Anisotropic filtering (step 2)

`rhi_init` queries `GL_MAX_TEXTURE_MAX_ANISOTROPY` once (16x on the Intel dev box; the extension
has been core since GL 4.6 and present as `EXT_texture_filter_anisotropic` on everything older
that matters) and every mipmapped texture takes it at creation. Only mipmapped ones: anisotropy
is about which mip a grazing pixel samples, and the game's grazing surfaces - the sea stretching
to the horizon, roofs and roads under a 12-28 degree camera - are exactly where trilinear
filtering picks a mip sized for the long axis of the pixel footprint and blurs the short one.
The dashboard System tab cycles 1/2/4/8/16x (re-applying to the textures already made, since the
parameter lives on the texture object) and `MARE_ANISO=N` pins it for a capture.

### Shadow casters (step 3)

The step was planned as a cross-frame cache of the static casters. Measuring the sun killed it:
`DAYNIGHT_PERIOD_DAYS` is 24 and the default speed is 0.333 days/s, so the sun swings about
**5 degrees per second** - and 45 at speed 4. Every cascade matrix would be dirty every frame,
and the cache would never be read. (It would work while paused and in the city view, where the
clock crawls to 0.05 days/s, but that is a narrow case for a second depth atlas and a blit.)

What the shadow pass was actually spending its time on is resolution it cannot use: one shadow
texel covers a good part of a house, and the cascades were casting the same 80k-triangle LOD 0
the main pass draws, three times over. So each asset now gets a **private caster geometry built
from its coarsest LOD** (2k triangles today, 600 once the assets are re-exported with
`tools/export_assets.py`), with its own instance buffer carrying every instance of that asset -
whatever LOD the main pass chose for it, and including the ones the occlusion queries hid, since
a hidden house still casts a visible shadow. It is built in the upload hook, because
`scene_asset_mesh` frees the CPU copy as soon as it has uploaded it; and it is a separate
geometry rather than a second `mesh_t` over the drawn one because the shadow pass runs before
the main pass and they need different instance sets.

| view | caster triangles per cascade | `gpu.shadow_ms` coarse / full |
|---|---|---|
| Seville city tier | 653k (326 instances x 2k) vs 819k at the drawn LODs | **5.15** / 6.07 |
| staged city (`MARE_DETAIL=city`) | 1.42M vs 1.85M | **2.29** / 4.32 |

A low-sun town view rendered with both settings differs in **0.11 %** of sampled pixels by more
than 12/255: the silhouette a 2k mesh casts at shadow-map resolution is the silhouette an 80k
mesh casts. `MARE_SHADOW_LOD=0` restores the old behaviour for comparison.

### Wind-driven sea (step 4)

`sea_update_waves(day, dt)` runs once a frame and derives five Gerstner waves from
`wind_at(RF.sea_center)`:

* **direction** - a fan around the wind direction, the spread narrowing from 0.95 rad in a light
  air to 0.28 in a strong one, so a gale runs in near-parallel lines and the doldrums are
  confused and crossed;
* **wavelength** - a geometric series (ratio 0.64) whose longest member grows with the wind, so
  a fresh breeze raises a longer and slower swell as well as a taller one;
* **height** - amplitude follows wavelength with `WAVE_STEEP` capping the steepness, and the
  whole field scales by `0.10 + 0.95 (strength - 0.18)`: the doldrums (strength ~0.35) leave a
  nearly glassy sea, a westerly (~1.2) a heavy one;
* **randomness** - each wave takes a fixed angle, length and steepness jitter and a phase offset
  hashed from `G.seed`, so no two games get the same sea and the five never crest together;
* **the wind is smoothed** over about two seconds, so crossing from one wind band into the next
  turns the swell instead of snapping it.

Phase is **accumulated** (`phase += k * speed * dt`) rather than evaluated as `speed * time`.
That matters because the wind changes the wavelength, and with `speed * time` any change in
speed multiplies a large `t` and jumps every crest across the sea; it also keeps the field
stable after hours of running, where `t` is large enough to lose precision.

**The waves speeding up and slowing down as the camera moves** was aliasing, not timing. The
detail grid's cells grow exponentially with distance from the look-at point - 0.14 world units
under the camera, 7.6 at the rim - and the shortest waves are 0.5-1 unit long. Sampling a
0.7-unit wave with a 7-unit cell produces a beat pattern fixed to the *grid*, and the grid is
centred on the camera, so the pattern slid whenever the camera did. `wave_sum` now takes the
world size of a cell at that vertex and fades out any wave shorter than 2-4 of them, so what is
drawn is only what is sampled. `MARE_SEA_NYQUIST=0` renders the artefact for comparison: the
whitecaps that should fade out a few dozen units away instead speckle to the horizon.

The ripple detail and the foam streaks now drift downwind too (the second layer 25 degrees off
the first, so the two do not reinforce into long rays across the scene), and everything the
detail grid adds over the flat world grid fades out with the grid itself, so the two meet
without a step.

Ships ride exactly this water: `meshes_bind_waves` uploads the same wave set to the instanced
mesh pipelines, which pass `cell = 0` (a hull samples the surface at a point, so nothing
aliases).

### The camera-speed artefact, second cause (step 4 follow-up)

The grid-aliasing fade was real but was not the cause the user saw. The wave set followed the
wind at the look-at point with a 2 s smoothing, so panning rotated every wave direction by a
few degrees over a few seconds - and a plane wave whose direction rotates by angle `a` sweeps
its crests sideways by `r * a` at distance `r` from the phase origin. Hundreds of units from the
origin that is several units per second, faster than the waves travel: the swell visibly sped
up or slowed down with the pan, in whichever direction the wind at the new place differed.

A wave set is now frozen once built. When the wind at the look-at point has swung more than
12 degrees or changed strength by 0.22 since the current set was built, a second set is built
and the two are **cross-faded by amplitude** over 6 s (`u_wave_blend`): two static fields
blending move no crest anywhere. The ripple-texture drift became an accumulated offset for the
same reason (`direction * time` jumps when the direction changes). Proof, with the new
`MARE_PAN` and `MARE_SEA_FREEZE` hooks: with the wave clock frozen, the frame after a 3 s pan to
a point is **pixel-identical** (0 of 1,440,000 pixels) to the frame after teleporting there,
while the live-clock control differs in 25 %.

### Tessellation (step 5)

The Intel driver (32.0.101.6733) crashes inside `glLinkProgram` when a tessellation control
shader reads `gl_TessLevelOuter/Inner` back after writing them (bisected from a trivial program
up: every other construct, every version header from 4.00 to 4.60 and the compatibility profile
link). The levels are therefore computed in locals and the built-ins written once. The
evaluation stage also samples with explicit LOD (no derivatives exist there).

Cost on the Intel iGPU, open sea at 22 units: `gpu.sea_ms` 2.8 tessellated against 1.8 for the
warped grid; the staged battle 1.6-1.8 either way. What it buys: real geometric chop from the
displacement map, cells of a constant ~9 px from the camera to 300 units (the warped grid's grew
from 0.14 to 7.6 units over 180), no fixed square, and patches that never slide with the camera.

Rules carried over: every effect has a toggle (dashboard checkbox and an environment variable)
so it can be measured off and on; GPU time per pass goes through `rhi_gpu_scope`; the two
reference views (fully zoomed-out map, 50-ship battle) and the scripted zoom are captured before
and after each step with `tools/metrics_summary.py`.
