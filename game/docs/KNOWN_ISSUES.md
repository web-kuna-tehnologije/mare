# Known issues and limitations

See CHANGELOG 0.6 for the issues fixed in the last bug-hunt (Maracaibo/Red Sea reachability,
hostile-destination idling, treasure fleet control, culture sources, garrisons, label overlap,
town caching, CSV columns, dashboard autoscroll).

Severity: **H** breaks play or data · **M** noticeable · **L** cosmetic / edge case.
"Framework" marks issues in v3 itself that we work around rather than fix.

## Rendering
- **M** Peljesac is a peninsula in the land mask (two cells thick, the Mali Ston bay behind it,
  the Ston channel in front - `mare.exe --mask=16.9,43.1,16,7` shows it) but *draws* as a low
  sandy spit: every texel of a two-cell strip is within the rings' shore profile band
  (`terrain_rings.cc`, reach 6 cells inland), whose beach-coast dome gives land at one cell
  from the water under half its relief, and the overview's coast class there is sand. The
  profile needs a "thin land" rule (relief from the strip's own width, rock where the export
  says the peninsula is rock) or a finer land mask.
- **M** The southern Dalmatian islands and Peljesac are drawn as strips one or two land-mask
  cells wide (0.22.3, `geo_coast.h`). The navigation grid's majority rule treats a one-cell
  strip as sailable, so a routed fleet can cross the Hvar or Korcula strip visually on the
  rocks; Peljesac is two cells thick and holds. A finer land mask (CELLS_PER_DEG 32) is the
  real fix; the strips were chosen over the old merged blobs because the blobs sat where the
  Neretva, Peljesac and Mljet channels run.
- **L** Rivers are painted on the surface and never carved (0.22.3 default): where a hairline
  stream crosses a slope it follows the ground like a drawn line rather than a bed, and no
  river is navigable. `MARE_RIVER_CARVE=1` restores the 0.22.1 ship-scale channels, which cut
  through the ranges. The authored river data to come replaces both.
- **L** The hinterland props (`gfx/scatter.cc`) are at the ship's scale and the painted
  land-use patterns at the texel's, so a grove's trees stand on a stipple of much smaller
  painted crowns, and vine bushes sit on painted rows of a different pitch. Props cast no
  shadow by design for now. Both are placed from the near ring, so they appear only within
  40 units of the look-at point and are rebuilt when it moves a quarter of the ring.
- **M** `--rhi=vulkan` and `--rhi=dx12` bring their device up, pass their self tests and then
  **delegate every draw to OpenGL**. They are not yet alternative renderers: the window's GL
  context owns the surface, and a swapchain would have to replace it rather than share it.
  The Options panel says "restart to switch" and the log says which backend is active, but the
  pixels come from GL whichever is selected.
- **L** A GL debug message "sampler (0) that has depth comparisons disabled, with a texture
  object with a non-depth format, by a shader that samples it with a shadow sampler" is printed
  to the console once per run. It predates 0.22 (the 0.21 build prints it too): some pipeline
  that includes `common/light.glsl` is drawn before `light_bind` has put the shadow atlas on
  unit 3, so the shadow sampler sees whatever colour texture sits there. Harmless on NVIDIA;
  worth tracing with `MARE_SHADOW_DEBUG`.
- **L** Looking at a mountainside from a few units away (camera tilt at the horizon, zoomed to
  the city tier) shows faint horizontal lines across the rock where terrain chunks meet; the
  tessellated edges are watertight, so this is the material texturing, not a crack.
- **L** Rivers on the far ring are drawn at the ring texel's width (0.31 units, ~9 km) and
  faded by how much narrower they really are; at the regional zoom the network therefore reads
  as thin pale lines of one width. A line-width that follows the river's own width needs the
  rivers drawn as ribbons rather than through the ring texture (docs/TODO.md).
- **M** At ~160 units of camera distance over a running sea (the detail grid's range) the
  whitecaps appear as a lattice of pale blobs following the sea grid's vertices - the fold
  term is per vertex there. Related to the tessellated-sea grid issue above.
- **M** A navigable river channel is 2.1 world units wide where a navigation cell is 4, so a
  fleet pathing up one can sit off the centreline and clip a bank. Tightening it needs either a
  finer nav grid along rivers or a centreline-following steering term in `fleet.cc`.
- **L** The sea's global grid is coarse away from the camera, and it is the grid that discards
  land (`v_h > 0.35`). A river channel therefore only fills with water once the tessellated
  sea patches reach it (camera inside ~90 units); at regional zoom a channel reads as a line.
- **L** The Vulkan render self test allocates a 64x64 image and a pipeline at start-up
  (~0.4 ms) only when the Vulkan backend is selected; it is not run under GL.
- **L** The split square yards' far LODs (LOD4/5, 700 and 250 triangles) decimate a thin spar
  to a sliver, which is why the sail's own procedural spar stands in below 44 px of projected
  spar (`YARD_ASSET_MIN_PX`); the loader's "LOD 5 extent differs" warnings for `yard_*` and
  the mast library are that same decimation and are harmless.
- **M** **Cliff coasts outside the resident chunks still rise as walls.** Where no terrain-v2
  chunk is resident the 0.17 cliff field lifts the first inland cells straight out of the sea;
  inside them the coastal fringe takes the authored relief and the shore mask decides cliff,
  beach or marsh (0.22). The islands lifted by `raise_small_islands` are rounded now but keep a
  plateau profile behind the beach ring.
- **L** The chunk atlas holds the overview only: a finer regional dataset under
  `assets/data_new` sharpens the rings' heights but its biome / splat / normal maps are not
  sampled yet (the overview's are, at 3 km). docs/TODO.md.
- **L** Beyond the far ring's border (320 world units from the look-at point) the coast is the
  bare 7 km raster again, so at the regional zoom the shallows and the surf band along a
  distant coast (the Aegean from a camera over the Tyrrhenian) show the raster's steps where
  the ring fades out.
- **L** The overview's land cover is heuristic (no WorldCover was supplied to the exporter), so
  the biome masks follow elevation, latitude and slope; cropland and built-up ground only
  appear once a build with `--landcover` is dropped in.
- **L** The GL context is created on the first adapter the driver offers (the Intel iGPU on a
  hybrid laptop) while `--rhi=vulkan` picks the discrete GPU for its report: an
  `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` export would move GL to the dGPU.
- **M** **The tessellated sea shows a faint grid at regional zoom.** Dark lines at the sea
  patch lattice (12 world units) appear on a running sea from roughly 50-90 units of camera
  distance. Reproduce: `MARE_CAM=-20,42,60,25 --seed=7 --skip=120.45`. `MARE_SEA_TESS=0`
  (the warped-grid fallback) removes it entirely, and `MARE_WAVES=0` removes it, so it needs
  both the tessellated path and wave displacement. Ruled out by A/B: the Nyquist cell fade (the
  per-patch `tp_cell` it used to key on is now computed continuously per vertex and the lines
  survive), foam, SSAO, the tessellation ceiling (32 and 48 look the same) and wave amplitude
  (halving it at this range only dims the lines). The remaining suspect is the outer/inner
  tessellation transition row along each patch border, which is inherent to the
  level-per-shared-edge scheme; fixing it properly means reworking how patch levels are chosen.
  Lowering the regional wave height (0.11) made it less prominent but did not remove it.

## Gameplay / simulation
- **M** Region borders follow the land BFS from the coast and look like straight diagonal seams
  inland; a smoothing pass or height-weighted distances would look more natural.
- **M** Regions are segmented at ~7° of coast (160 cap), so some merge historically distinct areas
  (Flanders currently includes Normandy). More pins/cuts in `regions.c` would refine this.
- **L** Colonial regions with only one hub and no cloth/arms imports stay at low satisfaction for
  years: intended dependence, but the AI does not yet target those shortages deliberately.
- **L** Player labour allocation, chartered settlements and the standalone region panel from the
  design are not implemented yet (see CHANGELOG 0.7 pending list).
- **M** Balance is a first pass. Spain starts with 32 ports and earns several times the income of
  other nations; AI treasuries inflate into the millions over decades (harmless but ugly in Stats).
- **M** Colonial goods keep European prices near the 4× cap for years: demand exceeds what the
  colonies produce. Intended scarcity, but the price signal between European ports flattens.
- **M** AI warships choose sieges only near their ports (260 units) and never coordinate; sieges
  of great cities practically never happen.
- **L** Fleets can't cross the map edges (no wrap); distances are equirectangular, not great-circle,
  so high-latitude voyages are ~30% too long in km terms.
- **L** Merges happen at most once per simulated day and only between non-warring owners; three
  settlements in a cluster may take several days to collapse into one.
- **L** Settlement names are generated from a small suffix list; duplicates get a numeric suffix.

## Rendering / UI
- **L** The terrain does not cast shadows into the shadow map (it only receives), so mountains
  never shadow valleys. Terrain relief is still shaded per pixel by the sun's N.L term. The
  chunks are coarse, LOD-switched and skirted, which as occluders produced LOD-mismatch bands,
  stripes along every chunk edge and self-shadow acne; casting would need a dedicated,
  skirt-free, LOD-stable caster mesh (see DESIGN_RENDERER.md).
- **L** Shadow cascades stop at ~300 world units, where towns and ships become icons anyway;
  beyond that everything is lit.
- **L** On software rasterizers (llvmpipe/Xvfb) the shadow pass costs ~2-4 ms/frame and the
  tonemap resolve ~13 ms (a `pow` per pixel for the sRGB encode); use `--no-shadows` or the
  dashboard toggle. Both are negligible on real GPUs - the Perf tab shows the per-pass split.
- **L** Gunsmoke puffs are alpha blended without sorting between themselves, so overlapping
  puffs can blend in the wrong order. Not noticeable at the sizes used.
- **L** Fleet labels are not part of the label collision system: a selected fleet's label can
  overlap a port name.
- **L** Terrain chunks are one draw call each (~50 visible); fine on GPUs.
- **M** *Framework:* microui hit-testing inside v3 uses raw window coordinates, so on HiDPI
  displays (Retina, Windows scaling > 100%) panels may respond slightly off from where they are
  drawn. Our world picking scales correctly.
- **L** Terrain heights are exaggerated and not geographic; coastlines are ~1–2° fidelity.
- **L** Storm visuals are debug-draw circles and rain lines; no particle/weather system.
- **L** `mu_text` wraps to the current layout cell: any new paragraph must be preceded by a
  full-width `ROW(1, -1, 0)` (`ui_text_full` does this) or it wraps into a narrow column.
- **L** Chronicle keeps the last 160 lines only; the technical run log has everything.

## Assets, LOD and streaming (engine scale 0.8)
- **M** The director budgets triangles per tier by scaling the hand-over thresholds, which is a
  quality knob, not a hard cap: a frame can still exceed the budget by the amount one level
  step saves, and on integrated graphics the city tier stays GPU-bound at LOD 0/1 (Intel iGPU:
  20-80 ms frames). The generated descriptors keep LOD 0 only above 220 px for that reason.
- **L** Residency is per asset, not per LOD: an evicted building comes back with all its LODs
  (~6-9 MB each through IO → DECODE → MAIN), and the wanted set is the tier's, not predicted
  from camera velocity yet. Every asset is requested at start-up.
- **L** `lod.pops` counts multi-level jumps the one-step clamp held back, so it cannot be a
  visible pop. It fires when the camera teleports (a scripted zoom whose start distance differs
  from the camera's, a detail-view entry), when an asset lands and a town's item list is rebuilt
  so `(port, item)` keys change meaning, and briefly when the director's threshold scale
  relaxes across a tier boundary; `mare.c` logs each occurrence at DEBUG with the camera
  distance. Keying town pieces by piece identity rather than index would remove the second
  cause.
- **M** On integrated graphics the city tier over a large town (~2 M authored triangles at
  LOD 0 plus shadows) runs at ~26 ms per frame with the CPU at 0.5 ms: GPU-bound, and the
  director's scale alone does not bring it under 16 ms. A per-tier LOD 0 ban or a lower city
  budget on such GPUs is the knob.
- **L** A town or asset fading between two LODs carries one dither pattern per instance, so a
  town dissolving into its icon overrides the asset's own LOD fade for that moment.
- **L** The first frame after the window opens costs ~90 ms (GL warm-up, first present) on
  every run; it is not asset loading (that is off the render thread now) and not steady state.
- **L** In unattended runs the detail-view camera edge-pans if the cursor is parked at a
  screen edge, so `MARE_DETAIL` screenshots are not pixel-reproducible the way `MARE_CAM`
  ones are.
- **L** GPU timer readings in the detail view can stay stale for seconds (same value repeated
  in the metrics CSV); frame time is the reliable number there.

## Visual rendering 0.9
- **M** Over deep ocean, straight-edged darker polygons still appear a few hundred units out at
  mid zoom. They are the seabed terrain seen through the water where terrain chunk coverage and
  LOD change, not a sea artefact: they are there with `MARE_CLOUDS=0` and with the sea's
  refraction on or off. The refraction removed the version of this that showed over *deep*
  water; what is left is the shallow shelf, where the water legitimately transmits the bottom.
  A terrain fix (seabed chunks drawn to the same LOD as their neighbours), not a water one.
- **L** The simulation ticks once every 15 s, so the date advances in jumps of about five days
  at speed 1 and the chronicle arrives in batches. Fleets are interpolated across the interval
  and so move continuously, but they are always showing the position of the last tick, up to
  15 s behind the simulation. `MARE_TICK_S=0.3` restores the old cadence.
- **L** Cloud shadows evaluate four octaves of gradient noise per lit fragment. Measured at
  +0.1 ms on the integrated GPU at a 150-unit zoom, but it scales with how much of the screen is
  lit geometry; `MARE_CLOUDS=0` removes the cost entirely.
- **L** The overhead cloud pass only runs when the camera is above the layer (60 units, roughly
  `cam.dist > 75`), so there is a band of zoom where clouds are visible neither above nor below.
- **L** Bloom uses a fixed threshold of 1.05 in exposure-scaled linear light; at high dashboard
  exposure settings more of the scene blooms than intended. `MARE_BLOOM=0`.
- **L** Unattended screenshot runs are not input-isolated: a stray scroll or key from the
  desktop reaches the window and moves the camera or pauses the clock, which shows up as a
  frozen or slowed simulation in a capture. Two measurements in this session were voided by it
  before the `sim.speed_eff` / `sim.cam_dist` metrics made it obvious.
- **L** The Intel driver (32.0.101.6733) crashes in `glLinkProgram` if a tessellation control
  shader reads `gl_TessLevelOuter/Inner` after writing them; ours never does. Any future
  control shader must follow the same rule (DESIGN_VISUAL.md, tessellation).
- **L** The tessellated sea costs ~1 ms more GPU than the warped grid on the integrated GPU in
  open water (`gpu.sea_ms` 2.8 vs 1.8); `MARE_SEA_TESS=0` or the dashboard's tess-level knob
  trade detail for time. The two other authored sea maps (`sea_texture_normals.png`,
  `tesselation_texture.png`) do not tile and are not used until `tools/sea_textures.py
  --seamless` has been run on them.
- **L** Ship foam is computed for the 24 ships nearest the look-at point; a crowded harbour's
  outer ships foam only as they come within that set. Moored ships leave no wake by design.
- **L** Weather states are pinned per run with `MARE_WEATHER`; in play the mist needs calm air
  at dawn next to a shore, which at the default day/night period happens for a minute or so per
  cycle.
- **L** SSAO is a post pass applied in the tonemap, so it darkens sun-lit pixels in a crease as
  well as the ambient there; the terrain's horizon occlusion is the only term that touches the
  ambient alone. Half resolution and eight samples: fine detail can shimmer slightly on hard
  edges. `MARE_SSAO=0` or the dashboard knob.
- **L** Refraction samples the frame drawn before the water, so a ship's hull refracts the
  terrain behind it rather than the water under it; the depth test falls back to the unbent
  sample when the bent one lands on something in front of the surface.
- **L** The RTS camera never tilts above the horizon, so the sky is a thin band; the sun disc
  is rarely in frame and the clouds are mostly seen as their shadows.
- **L** Under a low sun facing a bay the shallows read bright: sand refracted through a few
  centimetres of water plus the surf sheet and the broad specular of a low sun. `MARE_FOAM`
  and the dashboard foam knob tame the surf part.
- **L** The sea's detail grid is a fixed 180-unit square around the look-at point; waves exist
  only inside it. The surface detail now fades out with the grid so the boundary does not step,
  but a camera low enough to see past 180 units sees flat water beyond. The tessellated sea
  (DESIGN_VISUAL.md step 5) removes the square.
- **L** The staged city detail view shows no cast shadows from its authored buildings (the
  cascades are fitted to the world camera, not the staged scene). Unchanged by the caster work:
  the two caster modes render it identically.
- **L** 39 assets at ~8 MB of GPU meshes each is ~312 MB, over the 192 MB residency budget in
  `gfx/scene.c`; once the full set is exported the streamer will cycle assets in and out in the
  city tier. `tools/export_assets.py` warns about this.
- **L** Occlusion-query answers apply a frame or more late, so culling is suspended whenever the
  camera moves; a building that becomes visible while the camera is still (a ship sailing out
  from behind it does not count - only authored buildings are tested) can be absent for up to
  the query round trip (one to three frames on Intel).
- **L** One run on the Intel driver spent ~400 ms a frame for ten seconds while the first
  version re-uploaded instance buffers between the shadow and main passes and no query answer
  arrived; the re-upload is gone (lists are partitioned before their single upload) and a
  query unanswered for 90 frames is abandoned, and the stall has not recurred in six runs.
  `MARE_OCCQ=0` turns the queries off if it does.
- **L** Terrain never occludes anything at this relief and camera pitch (measured, see
  DESIGN_VISUAL.md); the terrain ray for towns and ships is kept but does nothing visible.

## Observability
- **L** `mare_metrics.csv` re-emits a header (prefixed `# columns changed`) when new scopes or
  metrics register after the first row; readers must tolerate comment lines (pandas: `comment='#'`).
- **L** Frame-time guides assume 60/30 Hz; the dashboard scale button cycles 16.7/33/66/133 ms.
- **L** Crash handlers write to the log from a signal handler (not async-signal-safe by the letter
  of the standard; acceptable for a dying process).

## Platform / framework
- The full game executable initializes GLFW before main, even with `--simulate`.
  On displayless Linux use `xvfb-run -a ./mare --simulate=60`; simulation tests are
  fully independent of the window system and need no X server.
- **Framework:** v3 caps frames at monitor Hz + 2; virtual displays report 0 Hz → 2 FPS. The game
  disables the limiter when the monitor reports < 10 Hz (`--fps=N` overrides).
- **Framework:** `app_width()/app_state()` do an X11 round-trip and a `glGetIntegerv` per call;
  we sample once per frame. Do not reintroduce per-object calls.
- **Framework:** on Linux, GLFW's joystick code includes `<linux/input.h>`, whose `KEY_*` macros
  shadow v3's `KEY_*` enum. Use `KDOWN/KHELD(GLFW_KEY_*)`.
- **Framework (patched):** `keyboard(glfw3).h` called `toupper()` on GLFW codes ≥ 256 (UB on
  glibc, F-keys never registered). Patched in `ext/ext-input/ext-keyboard/keyboard(glfw3).h`.
- Windows x64 Release and both test suites were verified locally on 2026-09-06, with
  a visual check of the running 3D game. Linux/macOS status depends on CI results.
- Save files are the raw `game_t` (≈1.1 MB) guarded by magic/version/size; they are not portable
  across compilers with different struct padding (x64 MSVC/GCC/Clang agree in practice). They
  are written to `<name>.tmp` and renamed over the previous file (remove + rename, since MSVC's
  `rename` will not overwrite), so a crash mid-write leaves the previous save intact; a crash
  between the remove and the rename would leave only the `.tmp`.
- Loading is asynchronous for the file read but the state swap rebuilds the caches on the main
  thread, and the port-distance rows cost ~300 ms of that (111 ports, parallel on 8 chunks): a
  load is one deliberate pause, not a hitch-free operation. A save never blocks the frame.

## Performance envelope
- Simulation ≈ 1 ms per 0.1-day step (≈110 ports, ≈130 fleets); speed 4 (20 days/s) uses ~20% of a
  core. Above ~40 steps per frame days are dropped with a logged warning.
- Port-distance rows are precomputed in parallel at game start and after a load (~300 ms); a
  founded colony still computes one row (~15 ms) inside the tick that founds it.
