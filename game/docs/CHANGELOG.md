# Changelog

## 0.9.0 (in progress) — visual rendering, steps 1-4
Plan and measurements: [DESIGN_VISUAL.md](DESIGN_VISUAL.md).

- **Wind-driven sea** (`gfx/sea.c`, `glsl.h`): the authored four-wave set is replaced by five
  Gerstner waves derived every frame from the wind at the look-at point - a fan around the wind
  direction that narrows as it strengthens, wavelengths growing with it, height from glassy in
  the doldrums to heavy in a westerly, and per-wave randomness hashed from the game seed. The
  wind is smoothed over ~2 s and the phase is accumulated per wave, so a wavelength that changes
  with the wind never makes the crests jump. Ships ride the same set.
- **Waves no longer speed up and slow down with the camera**: the detail grid's cells grow from
  0.14 to 7.6 world units, and waves shorter than a few cells were aliasing into a pattern fixed
  to the camera-centred grid. `wave_sum` now fades out any wave the grid cannot sample at that
  point. `MARE_SEA_NYQUIST=0` renders the artefact for comparison.
- Surface detail and foam streaks drift downwind (the second layer 25 degrees off the first so
  they do not reinforce into rays), and everything the detail grid adds over the flat world grid
  fades out with the grid, so the two meet without a step.
- **Shadow casters at a coarse LOD** (`gfx/scene.c`): the cascades cast from a private copy of
  each asset's coarsest LOD (2k triangles rather than the 80k the main pass draws), with their
  own instance buffer that includes the instances the occlusion queries hid. `gpu.shadow_ms`
  5.15 vs 6.07 in the city tier and 2.29 vs 4.32 in the staged city; a low-sun town view differs
  in 0.11 % of pixels. The cross-frame static cache first planned for this step is not viable -
  the day/night cycle swings the sun ~5 degrees a second, so every cascade is dirty every frame.
- **Anisotropic filtering** (`rhi_gl.c`): the driver maximum (16x here) on every mipmapped
  texture, with a dashboard cycle and `MARE_ANISO=N`. The sea and the roofs are seen nearly
  edge-on, which is where trilinear filtering blurs.
- **Five LOD levels** (`ASSET_MAX_LODS` 4 -> 5) and the asset export pipeline:
  `tools/export_assets.py` walks the whole `.blend` source tree, resolves Meshy filenames to
  asset names, skips what is already exported and runs Blender headless per asset
  (`--check` prints the plan, `--validate` runs Blender but writes nothing, `--jobs N`
  parallel); `tools/blender_export_glb.py` exports LOD0..LOD4 (80k/24k/7k/2k/600 triangles);
  `tools/gen_asset_descriptors.py` writes 5-LOD descriptors and knows ship parts.
  `tools/ppm2png.py` converts the `MARE_SCREENSHOT` captures.

- **GPU occlusion culling for authored buildings** (`gfx/occquery.c`, `rhi_query_*`): every
  authored instance's bounding box is tested against the depth buffer after the opaque pass
  with a hardware occlusion query; instances whose last answer was "hidden" are left out of
  the main pass (the shadow pass still draws them), answers apply a frame later, and nothing is
  culled while the camera moves. Keys are the LOD selector's per-instance keys. City tier over
  Seville: 104 of 155 houses culled, 16.8 → 15.3 ms; the rest of the frame is the shadow pass
  (step 3). Toggles: dashboard "occlusion", `MARE_OCCQ=0|1|2` (2 = measure the queries' cost).
- **Terrain-horizon occlusion measured and shelved** (`gfx/occlusion.c`, `tools/occ_probe.c`):
  a ray-march over the heightmap hides 1 town in 37k camera placements and no terrain chunk -
  the relief is 7 units high and the camera never pitches below 12 degrees. The ray stays for
  towns and ships (it costs nothing: 218 heightmap reads for the whole probe), the chunk test
  was removed. `asset_test` covers the ray on synthetic terrain.
- Metrics `gfx.occluded_assets`, `gfx.occ_queries`, `gfx.cam_moving`, `gfx.occluded_ports`,
  `gfx.occluded_ships`; `gfx.occ_queries` profiler scope; the Performance tab's occlusion line.

## 0.8.3 — LOD director, residency, async save/load, Jobs and LOD dashboards (engine scale, steps 5-10)
Design: [DESIGN_LOD.md](DESIGN_LOD.md), [DESIGN_JOBS.md](DESIGN_JOBS.md) (with the before/after
table); plan in [ROADMAP](../../docs/ROADMAP.md). The engine-scale plan is complete.

- **View tiers and the LOD director** (`gfx/lod.c`): city / battle / regional / strategic /
  continental, derived from the camera, each with a triangle budget. The scene build reports
  what it submitted and the director scales the hand-over thresholds for the next frame so the
  smallest instances drop a level first and recover as soon as there is room
  (`lod.asset_tris`, `lod.budget_scale`).
- **Icons and clustering**: marker alpha coupled to the mesh fade, size by importance, and
  screen-space clustering of distant markers at continental zoom (`lod.icons_clustered`).
- **Residency** (`gfx/scene.c`): every authored asset is wanted up to the regional tier; past it,
  assets untouched for 12 s are evicted coldest-first once the 192 MB GPU budget is exceeded
  and reloaded through IO → DECODE → MAIN on return, the town drawing its procedural pieces
  meanwhile. Shared cloud cover (`gfx/clouds.c`) for zooms that outrun the streamer.
- **Simulation on the pool**: the 111 port-distance rows (~15 ms of Dijkstra each) are
  precomputed in parallel at game start and after a load - 301-307 ms on 8 chunks instead of
  ~1.7 s serial, and no Dijkstra inside a tick any more. The per-entity tick phases measured at
  1-2 us each and stay sequential (DESIGN_JOBS.md, "What is not worth parallelising").
- **Asynchronous save / load** (`sim/game.c`: `game_save_async`, `game_load_async`,
  `game_io_poll`): the frame copies the ~1.1 MB state (0.1-0.3 ms measured) and the IO lane
  writes `<name>.tmp`, renamed over the previous save only when complete; a load reads and
  validates on the IO lane and the poll swaps the state in and rebuilds the caches (~300 ms,
  the nav rows). One operation in flight: a save during a save is queued, a load during
  anything is refused. F5/F9 and the menu use it; **autosave** every 180 s of unpaused play
  to `mare_autosave.bin` (`AUTOSAVE_SECS`, `MARE_AUTOSAVE_S` overrides), loadable from the
  menu. `sim_test` checks the asynchronous files are byte-identical to the blocking save's,
  the queue/refuse rules, that no `.tmp` is left, and that a state applied by the poll
  continues bit-identically.
- **Dashboard Jobs tab**: per-lane busy % over a half-second window, queue depth now / peak
  with a history graph, jobs/s, executed and busy totals, inline runs, time blocked in
  `jobs_wait`, the asset pipeline's stages in flight (`asset_t.stage`,
  `asset_pipeline_counts`), streamed textures, save/load state and cost, autosave countdown,
  save buttons. **LOD tab**: tier and every tier's budget, triangles vs budget and the
  director scale, tracked / fading / clamped-jump counters, clustering and culling counts,
  the bias knob, GPU residency per asset and LOD with idle time, streamed textures. Seven
  tabs; `MARE_DASHTAB=jobs|lod|...` opens one unattended.
- **Metrics and evidence**: `jobs.busy_pct`, `jobs.wait_ms`, `jobs.io_queued`,
  `save.snapshot_ms`, `io.save_ms`, `io.load_ms`, `save.count`; a DEBUG `long frame` record
  for any frame over 25 ms with the previous frame's CPU/GPU split, upload jobs, assets
  loading and save/load state; a DEBUG record whenever the LOD clamp acts, with the camera
  distance; `tools/metrics_summary.py` (median / p99 / max per CSV column);
  `MARE_ZOOM=...,delay` holds the camera before a scripted zoom starts.
- **Measured** (Release, 24-core Windows box, Intel integrated graphics, 240 Hz, `--dev
  --skip=120`, nine authored assets resident, autosave forced every 4 s; details in
  DESIGN_JOBS.md): fully zoomed-out map 4.5 ms median / 6.1 ms worst after start-up (was
  4.0 / 90.4 with one asset), staged battle 4.4 / 5.9 (was 4.0 / 90.2); the worst frame while
  an autosave was being written was 4.0-5.1 ms in both; start-up parses eight houses in
  365-380 ms concurrently and the church in ~1 s, all off the render thread. The scripted
  6 → 900 → 6 zoom over Seville runs at 5.2 ms median; its only frames over 25 ms are at
  6-10 units with ~2.1 M authored triangles at LOD 0 on the iGPU, identically with autosave
  on and off (the step-5 GPU limit in KNOWN_ISSUES, not a stall: CPU 0.5 ms). `lod.pops`
  recorded clamp activations only when the church landed mid-run and the town rebuilt, and
  as the director relaxed across the city/battle boundary - walked one level per fade, no
  visible pop. The first frame still costs ~92 ms of GL warm-up.

Known: residency is per asset and the wanted set is the tier's, not velocity-predicted; a load
pauses ~300 ms for the nav rows; the city tier is GPU-bound on integrated graphics.

## 0.8.2 — background assets, screen-space LOD with cross-fades, authored sea (engine scale, steps 2-4)
Design: [DESIGN_LOD.md](DESIGN_LOD.md), [DESIGN_JOBS.md](DESIGN_JOBS.md); plan in
[ROADMAP](../../docs/ROADMAP.md).

- **Assets load on the job system** (`assets/asset.c`): file reads on the IO lane, glTF
  parsing + parallel texture decoding + mesh processing on the DECODE lane into a private copy,
  commit on the MAIN lane (so `asset_t.state` only changes on the render thread), then one
  budgeted GPU-upload job per LOD. New states `ASSET_LOADING`, `asset_request[_all]()`,
  `asset_generation()` (town caches rebuild when an asset lands), `asset_set_upload_hook()`.
  Start-up no longer waits: six Meshy buildings parsed concurrently in 322 ms each and were
  resident 0.1 s after the window opened (the church alone used to block for 795 ms).
  `core/diag.c` logging took a spinlock since workers log; `jobs_help()` added for callers that
  wait on a state rather than a counter.
- **Streamed textures** (`gfx/textures.c`): request once, poll; IO → DECODE → MAIN.
- **Authored sea** (`gfx/sea.c`, `game/assets/sea`): a seamless tangent-space wave normal map
  and a foam albedo cut from the reference sheet by `tools/sea_textures.py` (margin cross-fade
  in both axes, vectors renormalised, high-passed albedo so the photo's gradient does not band
  when tiled). The shader samples both at the swell and chop scales; bright streaks become
  whitecaps over the depth colour. The procedural wave map covers until the tiles are resident.
- **Screen-space LOD** (`gfx/lod.h/.c`): one metric for everything with levels of detail - the
  projected diameter in pixels - with thresholds in reference-1080p pixels. Authored switch
  distances convert at use; descriptors may also give `px` directly. Ships `[full, simplified,
  icon]` at 80 / 26 px of fleet footprint, towns `[town, icon]` at 35 px, authored assets
  their own LODs: the pixel equivalents of the old 75 / 230 / 260 unit rules, so a 12-ship
  fleet and a great city stay 3D further out than a sloop and a hamlet, and 4K keeps the same
  on-screen detail as 1080p.
- **No popping** (`inst_t.dither`, `GLSL_DITHER`): per-instance state with ±12 % hysteresis and
  a 0.3 s cross-fade; both levels draw with complementary ordered-dither masks in the mesh and
  shadow passes (exact pixel partition, no sorting), icons fade with the complementary weight,
  mid-fade reversals play back instead of snapping. `lod.pops` (jumps of 2+ levels) stays 0 on
  the scripted 6 → 900 → 6 unit zoom (`MARE_ZOOM`; median frame 4.9 ms, `frame.cpu` < 1 ms
  throughout, up to 83 instances mid-fade).
- **Building set pipeline**: `tools/blender_export_glb.py` now exports LOD0-3 per `.blend`
  (authored `*_LODn` nodes or decimation to 80k / 24k / 7k / 2k triangles, base colour only,
  1k JPEG), `tools/gen_asset_descriptors.py` writes descriptors (`px` thresholds 220 / 90 / 30,
  per-role heights) and the manifest. Towns place authored houses by tier (merchant houses on
  the square, huts by the wall), plus warehouse / lighthouse / town hall when those roles exist;
  the staged city view uses the same set at 1:1. Procedural boxes remain the fallback.
- Dashboard: asset states include "loading", the Assets tab shows lane queue depths and LOD
  fade/pop counters; `asset_lod_switch_dist()` for px-based LODs. Tests: `asset_test` now
  drives `lod.c` (metric, hysteresis, fade, reversal, pops, determinism) and the asset
  generation; all four suites pass.

Known: no triangle budget yet, so the city view with ~150 authored houses is GPU-bound on
integrated graphics (step 5); every LOD of every asset is resident (step 7).

## 0.8.1 — job system (engine scale, step 1 of 10)
First step of the engine-scale work planned in [ROADMAP](../../docs/ROADMAP.md): a thread pool
every long-running piece of work will go through, so that tick processing, asset loading and
(later) LOD streaming stop stalling the frame. Design: [DESIGN_JOBS.md](DESIGN_JOBS.md).

- **`core/jobs.h` / `core/jobs.cpp`** — one worker pool of `cores - 1` threads serving four
  lanes rather than a pool per kind of work: `WORKER` (frame-critical, drained first), `DECODE`
  (glTF parsing, texture decode, mesh processing), `IO` (two dedicated threads, because a
  blocking read should not cost a core) and `MAIN` (GPU uploads, run only by the render thread).
  Join counters, `jobs_parallel_for`, per-lane statistics, and a budgeted `jobs_main_drain`
  called once per frame (`jobs.upload` scope, `jobs.main_ran` / `jobs.queued` metrics).
- **Waiting helps:** `jobs_wait` and `jobs_parallel_for` run queued jobs on the calling thread,
  so a job may wait on jobs it spawned without deadlocking and the render thread never idles.
- **Determinism by construction:** `parallel_for` chunk boundaries depend only on `(count,
  grain)`, never on the worker count, so a reduction into per-chunk partials is bit-identical
  however the chunks were scheduled. This is what will let the simulation tick move onto the
  pool without breaking save/load determinism.
- **First C++ in the project**, and deliberately the only one: C11 has no portable threads that
  MSVC implements, so the pool is one C++17 translation unit behind a C ABI. `mare.c` stays the
  C unity build. Rules for any future C++ island are in ONBOARDING.
- **Synchronous mode** (`MARE_JOBS=0`, and the default before `jobs_init` / after
  `jobs_shutdown`) runs every submit inline, so headless runs and race hunts need no special
  casing. `MARE_JOBS=N` pins the worker count.
- `tests/jobs_test.cpp` (new, in CTest and both build scripts) covers the lanes, counters,
  chunk determinism across worker counts, nested waits, the MAIN-lane thread affinity and
  budget, back-pressure at 3x queue capacity, and shutdown. Build plumbing updated for the
  second translation unit (`build.sh`, `build.bat` — `cl` rejects `/std:c11` with `/std:c++17`,
  so the C++ unit is compiled separately — `CMakeLists.txt`, CI).

Nothing submits real work to the pool yet; steps 2 (asset loading) and 7-9 (LOD streaming,
tick, save/load) are its clients. Measured idle cost: `jobs.upload` averages 0.001 ms/frame.

## 0.8.0 — detail views: through the clouds to the battle and the city
Spectator close-ups for the two reference concepts (`assets/ship_battle*.png`,
`assets/city_view.png`), staged with the current low-poly models as 1:1 samples.

- **The transition:** entering a detail view rolls procedural fbm clouds over the finished
  frame (~0.8 s, the "loading" bake), swaps to the staged scene behind full cover, then dives
  the camera from 380 to ~50-130 units as the clouds thin (~1.1 s). Leaving plays the same in
  reverse and restores the exact map camera. The overlay draws after the tonemap.
- **Battle view (`B`):** a staged local scene (1 unit ≈ 4 m) - open sea in every direction and
  a hilly coastline a kilometre off. Ships reuse the world meshes at true relative scale
  (sloop ~24 m, ship of the line ~52 m, capped at 50). A real battle is reconstructed from the
  engaged fleets (sides, ship types, nation colours); on quiet seas a 30-ship exercise is
  staged instead. Every ship sails independently: own course, speed and wander, steering back
  into the melee, rolling and pitching in the swell, trading broadside gunsmoke with the
  nearest enemy. Ship shadows land on the water (0.7.9's fix at work). Spectating is free -
  the world keeps simulating at the chosen speed.
- **City view (`V`):** the selected/hovered town (or the nearest one) at 1:1, laid out in
  districts that follow the port's actual state: civic centre (tower + banner), the authored
  church, a harbour front whose pier count follows B_DOCKS and warehouse row follows
  B_WAREHOUSE, a commercial core when B_MARKET exists, a residential mass sized by population,
  the wall ring with corner towers per fort level, and farms outside the walls. Two moored
  ships give the harbour life. The clock crawls at 0.05 days/s while inside (same rule as the
  map's city view); leaving restores the chosen speed.
- **Camera:** the ordinary RTS camera drives the local space (pan / Q/E / wheel / middle-mouse
  all work), clamped to the stage; `cam_pitch_scale` eases the pitch towards the horizon for
  the cinematic framing. Esc or M leaves. `MARE_DETAIL=battle|city` enters straight from
  startup for visual tests.
- **Plumbing:** world passes (terrain, sea, ports, ships, overlays, icons, labels) are skipped
  while a detail scene owns the frame; the scene submits through the standard instanced mesh
  pipeline, so PBR, gunsmoke and the shadow cascades apply unchanged (the shadow pass hooks
  `scene_submit_shadow`). World picking and commands are disabled; speed keys still work.
- Screenshots: `docs/screenshots/detail-battle.png`, `detail-city.png`. All three CTest suites
  pass.

Known limits: the staged sea is a calm plane (no waves or wakes); the city has no street grid
yet, only districts; battles are not steerable by design. Next feature: optimisation and
multithreading (see ROADMAP).


## 0.7.9 — shadows actually cast, city view, battle detail, picking by size
- **Fix (issue-2): nothing ever cast a shadow onto the ground or the water.** The shadow
  camera was placed on the wrong side of the scene (`center - sun*back` instead of
  `center + sun*back`): it looked *towards* the sun, so depth sorted away from it, an occluder
  above a receiver stored a larger depth than the receiver, and the compare always answered
  "lit". Only side-face darkening survived, which is what made shadows look "sort of there".
  Boats, houses, walls and the church now cast onto terrain and water. Verified by projecting
  logged cascade matrices against a dumped atlas, then in game
  (`docs/screenshots/shadows-fixed.png`, `-lowsun.png`).
- **Shadow filtering is bilinear now:** the depth atlas is sampled as `sampler2DShadow` with
  LINEAR + compare mode, so every PCF tap is a hardware bilinear depth comparison (3x3 taps on
  top). The depth margin dropped from 260 to 24 world units: the old range mapped ~550 units
  into [0,1], which pushed the lookup bias past the occluder separation of anything shorter
  than a few units - the second reason boats never shadowed the sea. Bias no longer scales per
  cascade (the normal-offset already does).
- **Dev tools:** `MARE_SUN="x,y,z"` pins the sun (lighting/shadow tests);
  `MARE_SHADOW_DEBUG=1` renders the shadow factor as grayscale, `=2` renders diagnostics
  (out-of-bounds / factor / enabled as RGB); the cascade-0 matrix is logged once under it.
- **City view (3rd zoom):** the camera now goes down to 2.5 units (was 6) and the pitch eases
  towards street level below 16; Q/E and middle-mouse already rotate. With the camera over a
  town the clock crawls to 0.05 days/s (the world does not race on while you look at a city;
  the HUD shows `~city~`), and the moment a battle is being watched from the same height the
  throttle lifts - spectating never slows the simulation. Resuming zoom restores the chosen
  speed automatically (G.speed is never touched).
- **Battle view:** a fleet that fired in the last 0.3 days keeps its full mesh and its whole
  line of ships out to 230 units (instead of 4 simplified hulls), budgeted at ~50 promoted
  ships per frame so a huge melee cannot stall the renderer. `B` jumps the camera to the
  freshest battle.
- **Picking:** the hover/click radius for towns and fleets now grows with their projected
  size on screen (matching the selection circles) instead of a fixed 16-18 px dot at the
  centre - a zoomed-in town is clickable anywhere on its footprint.
- **Proposal:** `game/docs/DESIGN_MAP_DETAIL.md` - how to get a more detailed map without
  raising the global grid: shader detail octaves near the camera, a coastline SDF to kill the
  cell staircase, and (only if needed) a camera-centred clipmap. With estimates and rejected
  alternatives.


## 0.7.8 — town placement fix, clearer water, asset roles and footprints (asset steps 8-9)
- **Fix (issue-1): houses in the fort wall, roofs without houses.** Town pieces were placed by
  centre point only: houses could stand in the wall ring (their roof overhang reached the
  stones), and a house on a slope took its centre height, so the downhill side floated and the
  uphill side was buried with only the roof showing. `town_build` (scene.c) now keeps a
  footprint list — tower, church, every house — and rejects overlaps; houses stay a margin
  inside the wall ring, take the *lowest* ground under their footprint, and are skipped on
  steep ground (rise > 60% of their height) or on the beach (below the sand/grass blend).
  Walls do the same per segment and are left out on the beach. Big pieces are placed first
  (tower, then the church landward of it, with a wider search on hillside towns), then as many
  houses as fit. Layouts change as a result; the per-port cache still rebuilds only when size,
  fort, owner or population change.
- **Sea:** more transparent — shallow water now shows the seabed clearly (alpha 0.38 → 0.92
  over the first 1.6 units of depth, was 0.62 → 1.0 over 1.1); foam and the map view are
  unchanged.
- **Asset roles (step 9, generator wiring):** descriptors carry `"role"` (defaults to the
  name); `asset_get_role(type, role)` returns the first loadable asset of that type and role,
  so the town generator asks for `building`/`church` rather than a file name — drop in another
  church descriptor with the same role and towns use it, no code change.
- **Collision footprints (step 8):** `asset_footprint_radius` (sphere, or the AABB's half
  diagonal) and `asset_contains_xz` (point test in the rotated instance frame) on top of the
  derived collision shape; the town generator uses the radius to keep houses out of the church.
  Still no physics — these are placement helpers.
- **Church assets:** `game/assets/church/Church_LOD0..3.glb` are now the real per-LOD exports
  (one node each: 128k / 50k / 15k / 3k triangles), copied from `/assets`.
- Tests: role parsing and lookup, footprint radius, rotated AABB containment. All three CTest
  suites pass. Screenshots: `docs/screenshots/town-placement-issue.png` (before) and
  `town-placement-fixed.png` (after, Seville), `sea-transparency.png`, `asset-church-towns.png`.


## 0.7.7 — asset runtime: GLB/glTF import, descriptors, normalisation, cache, LODs
The first authored 3D asset (a Meshy church) renders inside a procedural town. Everything
needed to go from a `.blend` to an instanced, shadowed, LOD-switched mesh in the game now exists
as a standard pipeline under `game/src/assets/` (CPU-side, GL-free, covered by the headless
`asset_test`).

- **glTF 2.0 / GLB loader (`gltf.c`):** GLB container and plain `.gltf`, base64 `data:` URIs,
  external `.bin` buffers (confined to the asset directory), POSITION / NORMAL / TEXCOORD_0 /
  COLOR_0, u8/u16/u32 indices, node TRS or matrix hierarchies baked into flat world-space
  streams, pbrMetallicRoughness factors, base-colour textures handed over as encoded bytes.
  Every primitive remembers its glTF mesh and node name. Missing normals are generated flat.
  Rejected with a message naming the mesh: sparse accessors, required extensions (Draco),
  non-triangle modes, accessor overruns, out-of-range indices, non-finite positions, truncated
  GLB chunks, a 4M-vertex budget.
- **Asset descriptors (`<name>.asset.json`, format in `asset.h`):** type (prop, building, ship,
  ship_part, terrain), per-LOD file *or node name* with switch distance, scale (`fit` height /
  length / width / max to a world size, or a plain factor), pivot (keep / center /
  base_center + offset), forward axis, material rules, collision shape. Validated on parse:
  missing name or file, paths escaping the directory, non-increasing LOD distances, LOD 0 not
  at 0, fit without size, negative scale, unknown enums.
- **Registry and cache (`asset.c`):** `game/assets/manifest.json` lists descriptors;
  `asset_get(name)` loads on first use and caches; failures are remembered (no retry storm) and
  reported with the file and reason; `asset_unload_all` drops CPU data but keeps registrations.
  Files shared by several LODs are read once. The asset root is `MARE_ASSETS` or `game/assets`
  searched upwards from the working directory. Startup preloads every registered asset (the
  church: 4 LODs, 128k triangles, 4096² JPEG decoded, 9 MB, ~220 ms).
- **Scale normalisation:** derived from LOD 0 so every LOD lands at the same size; sanity limit
  of 1000 world units catches cm/m mistakes.
- **Pivot normalisation:** `base_center` puts the origin under the model's footprint, so
  `inst_t.y = world_height` sits it on the ground; `forward` rotates the model's front to the
  game's heading 0 (+x).
- **Material metadata:** glTF materials map onto the game's 12-float PBR vertex format
  (roughness, metalness, colour, instance-colour slot). Rules match material names
  case-insensitively (`"*"` for all) and override roughness / metalness / colour, mark a
  material as taking the per-instance colour (nation roofs, flags), or disable texture baking.
  glTF's literal defaults (metallic 1, roughness 1) are read as "unspecified" and become a matte
  dielectric. The base-colour texture is **baked into vertex colours** (nearest sample at each
  vertex UV × baseColorFactor × COLOR_0); the renderer stays untextured.
- **LODs:** per-LOD meshes with switch distances; `asset_pick_lod` selects per instance from
  the camera distance (the church shows 128k / 50k / 15k / 3k triangles). A LOD with more
  triangles than the previous one logs a warning.
- **Collision (stub):** `collision.type` aabb / sphere parsed; radius and half extents are
  derived from the normalised bounds when not given. Nothing uses them yet.
- **Renderer:** `mesh_upload_indexed` (u32 index buffers via the RHI); scene.c keeps one
  instance list per asset and LOD, uploads GPU meshes lazily and frees the CPU copy; authored
  assets go through the same instanced pipeline as procedural meshes, so they receive the sun,
  cast shadow-cascade shadows and are frustum-culled with their town. Established towns
  (size ≥ 2) place the church on the landward side of the square, facing the harbour.
- **Tooling:** `tools/blender_export_glb.py` exports a `.blend` headlessly to GLB (textures
  embedded, optional decimated copy). GLB files are not committed (`game/assets/**/*.glb`);
  `game/assets/README.md` explains the layout.
- **Tests:** `asset_test` (CTest) builds GLB/glTF files in memory and on disk and checks baked
  transforms, generated normals, texture baking (2×1 BMP via data URI), scale / pivot / forward
  maths, material rules, LOD picking (including the bias), registry, cache accounting and 20+
  rejection paths.
- **Cache & LOD runtime (steps 6-7):** the church ships as four GLB files
  (`game/assets/church/Church_LOD0..3.glb`, one LOD node each selected by name); the texture
  bake cache is keyed by (model, image) so multi-file assets cannot cross-talk, and is bounded.
  Per-asset GPU bytes are tracked next to the CPU bytes; `asset.gpu_mb` and `asset.instances`
  land in the metrics CSV. A new dashboard **Assets** tab lists every registered asset (state,
  GPU MB, load ms, per-LOD triangles / effective switch distance / live instance counts) and
  holds the global `asset_lod_bias` knob: authored switch distances are multiplied by it, so
  0.5 drops to coarse LODs at half distance (performance) and 2 holds detail twice as far
  (quality). `MARE_DASHTAB=assets` opens the dashboard on any tab for screenshot runs.
  `MARE_LOD_BIAS` pins the knob for screenshot runs.
- **Fix — pivot per LOD:** LOD objects are usually scattered around the source scene (the Meshy
  export places Church_LOD0..3 at different translations), so normalising every LOD with LOD 0's
  pivot put LOD 1-3 metres away from the instance position and sank them into the terrain. Each
  LOD now derives its own pivot from its own bounds (scale still comes from LOD 0, and a warning
  fires when a LOD's extent differs from LOD 0 by more than 1.5x - a wrong node pick). The
  headless test gives the LOD 1 node a translation to pin this down.
  Verified in game: camera at 9 / 55 / 110 world units picks LOD 0 / 1 / 2, and
  `MARE_LOD_BIAS=0.04` shows the 3k-triangle LOD 3 in place next to the 128k LOD 0
  (`docs/screenshots/asset-church-lod0.png` vs `-lod3.png`, same town, same camera).
- **Fix:** `lighting.c` used `near`/`far` as local names, which `windef.h` defines as empty
  macros; the game did not compile with the current Windows SDK until they were renamed.

Known limits: textures are baked per vertex, so surface detail is only as fine as the mesh
(the 128k-triangle church reads well, a 500-triangle crate would not); normals pass through
non-uniform node scales approximately; one base-colour texture per material; Draco / meshopt
compressed files must be re-exported uncompressed.


## 0.7.6 — profiling hooks, slower clock, ship motion, visual fixes
Completes the renderer modernisation (step 10) and slows the simulation down to a watchable pace.

- **Profiling (step 10):** per-pass GPU timer queries read back without stalling, RHI VRAM
  accounting (textures / render targets / buffers), and process RAM. All shown in the dashboard
  Perf tab and written to the metrics CSV (`gpu.*_ms`, `mem.vram_mb`, `mem.ram_mb`).
- **Simulation speed:** keys 1-4 are now 0.33, 0.5, 1.0 and 3.0 days per real second (were 1, 3,
  8, 20), with 0.33 the default. A voyage now reads as a voyage.
- **Ship movement:** fleets are drawn from a smoothed render position that chases the simulated
  one, so ships sail continuously instead of stepping once per 0.1-day tick (measured: 387
  distinct render positions across 47 simulation ticks). Rocking gained a pitch axis and a second
  frequency, and its amplitude now follows what the ship is doing - moored, under way, or in a
  storm. The shadow pass uses the same vertex motion, so shadows follow.
- **Visual fixes:**
  - Detail normal maps are mipmapped, and the sea derives extra roughness from per-pixel normal
    variance (`dFdx/dFdy`). Together these remove the field of white fireflies that covered the
    ocean at middle and long range; a real sun glitter path remains up close.
  - Cannon fire was a v3 debug-draw wire sphere that read as a gold scribble. It is now real
    gunsmoke: an instanced puff on a translucent pipeline drawn after the sea, swelling and
    drifting, excluded from shadow casting.
  - Billowed sails use a blended normal, so a sail reads as one curved surface instead of four
    hard light and dark wedges.
  - Shallow water was muddy where the seabed showed through; the shoal tint is brighter and the
    transparency ramp shorter, so shallows read as water over sand.
- `MARE_CAM="lon,lat,dist[,yaw]"` pins the camera for reproducible screenshots.


## 0.7.5 — normal maps, frustum culling, instancing (renderer steps 7-9)
- Procedural tileable normal maps baked at init (`detail.c`): scrolling two-scale ocean waves on
  the sea (replacing the hand-written sines) and triplanar plank/plaster grain on the meshes,
  which carry no UVs.
- The sea is transparent in the shallows (seabed visible, foam stays opaque, map view solid) and
  piers and flagpoles now cast shadows, so docks shade the water under them.
- Per-object frustum culling for ports and fleets with a shadow-caster margin; culled counts in
  the dashboard.
- Instance buffers upload once per frame and are reused by the shadow cascades and main pass;
  the RHI reports draws/instances/triangles/streamed bytes per frame (dashboard + metrics CSV).
- Depth-precision fixes against triangle flicker: projections remapped from D3D-style clip z to
  GL's full [-1,1] range (camera and cascades), and double-sided quads no longer emit an exactly
  coplanar mirrored twin (shading is two-sided since 0.7.4).


## 0.7.4 — linear/sRGB, PBR, directional lighting, shadows (renderer steps 3-6)
- **Linear/sRGB pipeline:** author colours are converted to linear on input, all lighting runs in
  linear space, and `hdr.c` encodes to sRGB once after a Reinhard luminance shoulder.
- **PBR materials:** Cook-Torrance GGX + Smith + Schlick with hemispheric ambient. Mesh vertices
  carry roughness/metalness set as builder state (`mb_material`, presets for wood, paint, cloth,
  stone, tile, metal); terrain roughness follows slope/snow and the sea is a smooth dielectric
  with a real sun glint and Fresnel sky reflection.
- **Directional lighting:** one sun shared by terrain, meshes and sea (`lighting.c`:
  `render_light_update` + `light_bind`), replacing the per-shader ad-hoc diffuse+specular.
- **Shadows:** three sun cascades in one depth atlas, fitted to each frustum slice's ground
  footprint, texel-snapped, with normal-offset bias and 3x3 PCF. Ships, towns, forts and piers
  cast; the terrain receives only (see KNOWN_ISSUES). `--no-shadows` and a dashboard toggle.
- Mesh shading is now two-sided: sails, flags and roof slopes are single quads with inconsistent
  winding and previously fell to the ambient floor under PBR.
- Scene geometry is built once per frame and submitted twice (`scene_build_*` / `scene_submit_*`),
  so the shadow cascades reuse the main pass's instance lists.
- RHI gains depth-only targets, sub-rect viewports and depth readback; dev tools:
  `MARE_SHADOWDUMP`, dashboard exposure/shadow controls.


## 0.7.3 — HDR rendering (renderer modernisation step 2)
The 3D scene renders into an RGBA16F offscreen target and is tonemapped to the backbuffer
(`gfx/hdr.c`); UI draws on top. RHI gains render targets (`rhi_target_*`), backbuffer binding
and pixel readback. The tonemap is identity below 1.0 with a hue-preserving rolloff above, so
visuals are unchanged until later steps emit real HDR values. `hdr_exposure` multiplier;
`MARE_SCREENSHOT` env var captures the backbuffer to a PPM for smoke tests.


## 0.7.2 — RHI: API-agnostic renderer abstraction (design: docs/DESIGN_RENDERER.md)
First step of the renderer modernisation. All game rendering now goes through an API-agnostic
RHI (`gfx/rhi.h`): pipelines with baked depth/blend state, geometry objects with streamed
instance buffers, format-enum textures and cached-location uniforms. `gfx/rhi_gl.c` is the only
game file calling OpenGL; the backend include in `mare.c` is the single switch point for future
Vulkan/DX12 backends. `gpu.c` keeps the mesh builder and `mesh_t`/`inst_t` API on top of the RHI.
Framework fix: MSVC-only `#pragma comment(lib, ...)` lines in `ext` guarded with `_MSC_VER`
(clang-based Linux builds failed on dependent-library specifiers). No visual change intended.


## 0.7.1 — recovered Windows baseline (2026-09-06)

- Reconstructed source history with the supplied v3 archive and development docs.
- Fixed GNU-only region report expression for MSVC and colony hinterland identity.
- Added regression coverage, root build/launch scripts and pending-feature roadmap.
- Restored safe GLFW key normalization. Windows Release and both CTest suites pass;
  actual 3D game launch verified. See ../../docs/RECOVERY.md for precise scope.

## 0.7 — hinterlands & trading hubs (design: docs/DESIGN_HINTERLAND.md)
Implemented: region generation from coast segments + land BFS (`world/regions.c`, 160 regions, named
pins), climate potentials + rare deposits (iron/coal/copper/silver/gold, noise + historical pins),
hub attachment (≤3 per region, shares follow prosperity/traffic), region tick (`sim/region.c`: rural
production into export pools, rural consumption fed through hubs with two-way throughput, development
tiers, prosperity, inland mines/farms founded at tier crossings), ports as hubs (region demand deepens
markets, staples arrive from the region instead of the old abstract share, inland sales tax,
seigniorage, iron boosts arms output), directed colonisation (attractiveness, AI picks the best coast,
three-hub limit unless at war), Regions map overlay (development shading + borders), inland settlement
icons, Hinterland section in the port panel, site panel attractiveness. Save format v4; new tests.
Pending from the design: player labour sliders per region, chartered settlements, interior regions
switching hubs, rivers as throughput pins, region panel, copper hull upgrade, rare-good shipyard discount.

## 0.6 — bug hunt
Found by log mining (2,733 `no route` records), sanitizer runs and the known-issues list:
- **Data:** Maracaibo's harbour snapped into Lake Maracaibo (unreachable; merchants spawned there were
  stuck and re-planned forever). Gulf of Venezuela added to the coastline, port moved to the gulf.
  Somalia pulled 0.8° off the map edge so the Red Sea/Gulf of Aden connect to the Indian Ocean:
  Jeddah and Mocha are now reachable. New test: every port reachable from Amsterdam.
- **Fleets:** a fleet whose destination became hostile/blockaded/absorbed now logs why and diverts
  to the nearest friendly port (was: silent idle at sea).
- **Settlements:** foreign immigrants now come from the nations whose ships actually call (docked
  fleets + lane partners) instead of a random nation; Fort levels now garrison a town
  (`FORT_GARRISON_CALM` divides culture unrest), giving the player a counter to post-conquest revolts.
- **Treasure fleet:** when playing Spain the flota is under the player's command (AI no longer
  re-orders it); silver still pays out on docking at a home port.
- **Rendering/UI:** greedy label collision avoidance by priority (selected/hovered > yours > size);
  town instances cached per port and rebuilt only when size/fort/owner/population change
  (`scene_reset_caches()` on new game/load); dashboard log autoscroll pauses while scrolled up.
- **Diagnostics:** metrics CSV waits 2 s before its header and re-emits a `#`-prefixed header when
  columns change.
- **Navigation:** founding a colony computes one distance row instead of invalidating all (0.5).

## 0.5 — colonisation expansion
- Free colony placement with climate/terrain resource potentials, labour allocation,
  population capacity & immigration, cultures, assimilation, civil rebellion, settlement merging,
  AI coast claims. Production/Population panels. Save format v3. `nav_ports_added()`.
## 0.4 — cross-platform hardening
- Windows audit (MAX_PATH collision, stack sizes, POSIX in tests, console colours, NOMINMAX),
  HiDPI mouse scaling, strict C11 compliance, CMake project, GitHub Actions matrix, build.bat rewrite.
## 0.3 — observability & performance
- `diag` (leveled file logging, metrics, profiler, crash handlers), dev dashboard, metrics CSV,
  render-pass toggles. Root-caused the 2 FPS cap (monitor 0 Hz), removed per-call `app_width()`
  (−16 ms/frame), cached market norms (−30% sim time), O(1) rejection of cross-basin paths.
## 0.2 — restructure
- Split into core/data/world/sim/gfx/ui modules; simulation made framework-independent and
  deterministic (rng/history/timers in `game_t`, synchronous distance rows); stable fleet ids;
  save header; structured chronicle with listener; fine-grid path smoothing; headless test suite.
## 0.1 — first playable
- World, economy, trade AI, pirates, warfare, sieges, events, history, rendering with LOD and
  map view, microui HUD, save/load.
