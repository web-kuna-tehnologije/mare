# Known issues and limitations

See CHANGELOG 0.6 for the issues fixed in the last bug-hunt (Maracaibo/Red Sea reachability,
hostile-destination idling, treasure fleet control, culture sources, garrisons, label overlap,
town caching, CSV columns, dashboard autoscroll).

Severity: **H** breaks play or data · **M** noticeable · **L** cosmetic / edge case.
"Framework" marks issues in v3 itself that we work around rather than fix.

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
- **M** Over deep ocean a straight-edged darker region can appear a couple of hundred units out:
  it is the seabed terrain showing through the translucent water where the terrain chunk
  coverage changes, not a sea artefact (it is still there with `MARE_WAVES=0`, and gone with
  `MARE_PASSES=sea`). It predates the wind-driven sea.
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
