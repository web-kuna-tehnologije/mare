# Design: visual rendering 0.9

Status: **step 1 done** (occlusion culling; its section is below). The plan below is the feature "visual rendering" broken into
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
2. [ ] **Anisotropic filtering** - `rhi_texture_make` asks the driver for its maximum anisotropy
       and applies it to every mipmapped texture (asset base colour, sea normal/foam, detail
       maps); dashboard shows the level, `MARE_ANISO=N` pins it. Fixes the smeared sea and
       roofs at grazing angles.
3. [ ] **Static shadow caching** - towns do not move: their casters are rendered into the
       cascades only when the sun or the cascade's snapped centre changes, and kept otherwise;
       ships (dynamic) are rendered every frame on top. Cuts the shadow pass to the ships in a
       steady view.
4. [ ] **Wind-driven sea** - the fixed four-wave set and the value-noise wobble go. A wave set is
       generated from the wind at the sea's centre (`wind_at`): directions spread around the wind
       direction, wavelengths and amplitudes from the wind strength, randomised by a hash so no
       two seas look alike, and the whole field moves downwind. Fixes the waves speeding up and
       slowing down as the camera moves: the detail grid's cells grow with distance, and a wave
       shorter than a few cells aliases into a pattern that slides with the grid; each wave is
       attenuated below the local cell size (Nyquist), so only well-sampled waves displace.
5. [ ] **Tessellation and normal maps** - a GL 4 tessellation path for the sea behind the RHI
       (control/evaluation shaders, distance-based levels) with the warped grid as the fallback;
       the authored normal map stays the sub-wave ripple detail and adds a wind-stretched
       sampling direction.
6. [ ] **Foam and breaking waves** - whitecaps on crests from the Gerstner Jacobian (fold =
       foam), persisting a moment and advected downwind; surf: waves that reach the shallows
       steepen and break into a foam band over the beach; bow and wake foam around moving ships
       and splash around ships in a storm.
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

Rules carried over: every effect has a toggle (dashboard checkbox and an environment variable)
so it can be measured off and on; GPU time per pass goes through `rhi_gpu_scope`; the two
reference views (fully zoomed-out map, 50-ship battle) and the scripted zoom are captured before
and after each step with `tools/metrics_summary.py`.
