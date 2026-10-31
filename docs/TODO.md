# Terrain chunks and the living hinterland (0.22 plan, 2026-10-31, worktree `oak`)

The exporter under `assets/data_new` (`MARE_INTEGRATION.md`, format `mare-terrain-v2`) is now the
**only** terrain source: height, coverage, biome IDs, material weights and normals per chunk.
Every earlier cache of the same data was removed on 2026-10-31 - the v1 `assets/data/export-global`,
the u16 cuts (`assets/terrain/tiles`, `assets/elm/terrain/tiles*`, `gebco`), the fine Mediterranean
cut the height atlas read, and the procedural `.mht` ring caches - together with the code that
read them (`world/terraintiles`, `gfx/heightatlas`, `terrain/atlas.glsl`, `tools/terrain_*.py`).
The GEBCO GeoTIFF source cache (`assets/data/cache/gebco`, 7 GB) stays: it is what the exporter
needs to cut a finer regional build, and it is not a game cache.

Items 1-3 are done in this session; the rest is the order to take them in.

1. [x] **Terrain-v2 chunks in the world.** `world/geochunks` reads every dataset under
       `assets/data_new/*` (finest first, `longitude_wrap` honoured, `format` checked), streams
       all of a chunk's maps on the IO/DECODE lanes, feeds the clipmap rings (height, drawn by
       the tessellated patches) and a GPU **chunk atlas** (`gfx/chunkatlas`) of biome, splat and
       normal maps the terrain and sea shaders sample with the vertex-aligned texel rule.
2. [x] **Biome material masks.** `terrain.frag` takes the export's splat weights
       (grass/rock/sand/snow) and biome classes (trees, shrubs, grass, crops, built-up, bare,
       snow, water, wetland, mangrove, moss) as the ground's material masks, blended across the
       3 km sample so classes meet along organic edges; the export's normals light the ground
       outside the near ring.
3. [x] **Natural coasts.** A per-ring **shore mask** (cliff, beach, marsh, exposure) built from
       the chunk data and the Euclidean coast field: curved sandy beaches where the data is gentle
       and sandy, rock faces with deep water where it is steep, marsh where it is wetland; the sea
       breaks on them - wave sets ("tidal" surges every ~40 s), foam and spray on the cliffs, the
       swash running up and down the sand.
4. [x] **Map view modes**: depth, religion, political, port/settlement influence, production
       (HUD "View" button, `common/mapmode.glsl`, the territory texture's influence channel, a
       region-info texture). Religion is a per-nation table, not simulated yet.
5. [~] **Rivers** (`world/rivers.h/.cc`) - *generated network kept, carving off since 0.22.3;
       waiting for the authored river data*: a priority flood over the folded heightfield gives
       every land cell its downstream neighbour, drainage is accumulated down those directions,
       cells draining over 45 land cells are river; polylines are Chaikin-smoothed with a small
       meander and a half-width from the catchment. The rings carve the channel (flat bed, sloped
       banks) and write a river texture the terrain shader paints with flowing water; the lower
       reaches of the great rivers are nav-grid water by class (`navriver`), and a fleet may enter
       as deep as its largest hull allows (`nav_river_limit`: sloops and fluyts the upper
       reaches, merchantmen and frigates the estuaries, galleons never). No geometry-stage ribbon:
       painting the carved bed through the ring texture needs no extra pass and follows the
       tessellated ground exactly. Left: rain weighting by biome, deltas, lakes as basins.
6. [x] **Roads** (`world/roads.h/.cc`): every inland settlement is routed to the hub port of
       most influence in its region, and a region's hubs are chained, by an A* over the land
       mask whose cost grows with the slope crossed and halves along cells already carrying a
       road (farm tracks merge into one road to town). Class (track / road / highway) follows
       `traded_today / throughput`, rebuilt every 45 simulated days. Painted through the ring
       texture's A channel as packed earth. Bridge points are recorded where a route crosses
       a river (57 at seed 7) - the props for them are item 10b below.
7. [ ] **Hinterland settlements**: procedural villages along the roads, surrounded by what the
       region produces (vineyards, wheat fields and mills, orchards, pasture, timber).
8. [ ] **LOD streaming for scatter**: trees, rocks, fields, houses as instanced LOD chains paged
       per chunk; forests from biome 10, built-up from 50.
9. [ ] **Procedural micro-detail levels** on the near ground (furrows, stone walls, paths).
10. [ ] **Props**: export the `assets/buildings/city` Meshy blends (windmill, bakery, well,
        fences, roads, quay...) through the LOD exporter into `assets/props/` with LOD chains.

### Small items for the hinterland (requested 2026-10-31; each is an afternoon, in this order)

- [ ] **8a. Scatter framework.** One `gfx/scatter.cc`: per resident ring texel a deterministic
      hash decides what stands there from the biome class + splat + slope + the road/river
      textures; instances batched per prop type with 3 LODs (mesh / card / nothing), paged
      with the ring, frustum-culled per 8x8 texel block. Everything below is a rule in it.
- [~] **8b. Trees.** 0.22.3: the woods are a *canopy* in `terrain.frag` (crown noise, tilted
      normal) and the orchards/olive groves have real trees (`gfx/scatter.cc`); wild woodland
      still has no instances. Tree-cover class 10 -> clumps of 3-7 broadleaf/pine cards per
      texel, density by splat.grass; conifers above 2.5 units. Far LOD is the forest colour.
- [ ] **8c. Rocks.** Canyon rims and sea cliffs (shore mask R > 0.5, slope > 0.3): 1-3 boulder
      meshes per texel, scaled 0.02-0.08 units, tinted limestone/basalt by latitude as the
      shader does; scree slopes get pebble cards.
- [x] **7a. Fields.** 0.22.3, as a *texture*: the land-use ring's R channel (cropland on
      gentle low ground, thickest near villages and roads) painted as hedged strip parcels in
      one orientation per 1.5-unit district, each parcel its own crop, furrows up close.
      Left: align the district to the local road; wheat by the region's dominant good.
- [x] **7b. Vineyards and orchards.** 0.22.3: G channel (wine regions and the Mediterranean
      belt, sunny slopes, in patches) as vine rows along the contour between dry-stone walls;
      B channel (orchards round villages, olive groves on the belt's slopes) as a jittered
      crown grid, plus low-poly trees and vine bushes as props (`gfx/scatter.cc`, no shadows).
- [~] **7c. Villages.** 0.22.3, as a texture: the A channel (each inland settlement's built
      disc, roadside hamlets) painted as roofs on a loose grid with yards between. The asset
      houses (`small_stone_house` etc.), a mill where the good is Grain and a well are still
      to do; the procedural icon stays past 300 units.
- [ ] **10b. Bridges.** At each `road_bridges[]` point a stone-arch or timber-beam prop
      spanning `hw`, aligned to the road direction; the road texture already runs across.
- [ ] **6b. Road upgrade visuals.** Class 1 roads get a paler, wider surface with a ditch line;
      class 2 a cobbled texture (atlas cell 15) and milestones every 2 units.
- [ ] **9a. Micro-detail.** Stone walls along field edges and vineyard rows, footpaths from
      houses to the road, a rutted centre on tracks - all as ring-texture channels or card
      scatter, never geometry in the height.

## issue3, item by item (assets/issues/issue3/issue.txt, 0.22.6)

| Item | Status |
|---|---|
| thicken the mast again | done: `MAST_SLIM` 0.425 -> 0.52 |
| increase height of back mast on carracks and cogs | done: every aft station raised (see CHANGELOG) |
| shipyard.cc data into files | done: `assets/ships/ship_classes.json`, built-in table is the fallback |
| easily increase density of forests | done: `MARE_FOREST_DENSITY` |
| add LODs to ambient props | done: mesh within 7 units of the look-at point, crossed cards beyond |
| rigging to connect cloth sail to lantina, -15% thickness, knots, foot guide | done: robands, foot rope, sheet and tack; spar 15% slimmer; head gap 0.014 -> 0.004 |
| divide lantina from front mast | done: tack hauled in 0.44 -> 0.30 mast heights, peak further aft |
| oars too low in water | done: ports at the rail, steeper blade, half the dip |
| fences, vineyards, grain, orchards as 3D | done: fence panels on the parcel edges, standing corn; vineyards and orchards were already props |
| mines and villages with mills | done: a windmill per farm village, a pithead per mine |
| sail classes + normal maps, quality | done: premium/used albedo+normal, per-ship (tools/ship_textures.py) |
| 2 types of rigging, premium and used | done as two cordage colours per class, chosen per ship. The *texture* is not on the rope: at a 1-2 px rope width it would read as noise - the colour is what carries |
| finish the todo list, skip rivers | items 7-10 below; rivers untouched and still painted-only |
| forests in bunches, not in the fields | done: clump mask, cropland excludes woodland outright |
| fields bigger, more noticeable, different sizes | done: parcel size per district, more contrast |
| reduce zoomed-in terrain height (Po like Switzerland, Alps like spikes) | partly: the max(data, cell) pyramid artefact is gone and the world is 20% lower with `MARE_HEIGHT_SCALE` to tune. A *zoom-dependent* height was rejected - every height threshold and every prop would move as the camera came in. The remaining lumpiness is the 2.8 km sample spacing: roadmap item 1 |
| ship wakes in 3D with foaming | done: Kelvin arms and the lane astern, as geometry, with their own foam |
| reflections | done: screen-space, against the captured scene (`MARE_SEA_SSR=0` to compare) |
| wind against the ship: move backwards and rotate | done as **leeway**: she falls off to close-hauled and is set to leeward. True sternway was tried and reverted - without tacking in the pathfinder a windward leg never finishes (two-year trade 19,700 -> 6,800). Tacking is the proper fix and is not done |

### TODO items 7-10 after this round

- **7a fields** - done (texture) + fences and standing corn as props.
- **7b vineyards and orchards** - done (texture + props).
- **7c villages** - done as far as procedural props go: houses, a windmill, a pithead. The
  authored `small_stone_house` assets and a street layout still want the settlement generator.
- **8a scatter framework** - done (`gfx/scatter.cc`): one rule per prop type, paged with the
  near ring, LOD split, density knob.
- **8b trees** - done: forest broadleaves and pines in stands, orchard and olive trees.
- **8c rocks** - done: boulders on bare steep ground.
- **9a micro-detail** - partly: fences are the stone-wall/hedge item; footpaths and rutted
  track centres are not drawn.
- **10 props** - the Meshy `assets/buildings/city` blends are still not exported through the
  LOD pipeline; the props above are procedural meshes, not authored assets.
- **10b bridges** - not done: `road_bridges[]` is recorded, nothing is drawn on it.

## Session notes (2026-10-31 afternoon, 0.22.5 - issue2)

- Mirror `C:\projects\mare_oak_20261031_120901` (own `assets\elm` + `assets\data_chunks_terrain`
  copies). Helpers: `build.bat [test]`, `shot.bat lon lat dist yaw file secs [tilt]`,
  `rig.bat dist yaw file`, `cine.bat dist yaw ox oz pitch file` (staged camera with pitch -
  the cinematic view), `mask.bat`, `modeshot.bat`.
- `assets/dev_oak_<stamp>.txt` is dev.txt with the boxes checked against the code; a copy and
  `summary_oak_<stamp>.md` live in `docs/dev/`.
- The `assets/example` frames are read in `game/docs/DESIGN_VISUAL_TARGET.md`.
- `mare-world-terrain` was not touched (read only, and only in 0.22.3 to locate the datasets).

## issue2 / next feature.txt - the ocean checklist against the code (0.22.5)

| Roadmap item | In the engine |
|---|---|
| Coarse ocean patch grid | yes: 50x50 quad patches, radius 300, plus an inner lattice near the camera (`sea.cc SEA_PATCH_*`) |
| Tessellation control shader | yes, `sea/sea.tesc` (per-edge level from screen size, shared edges) |
| Tessellation evaluation shader | yes, `sea/sea.tese` (Gerstner sum + displacement map + ship waves) |
| Distance / screen adaptive | yes: level = pixels per unit x patch / distance / 8 px per cell |
| Factors far 1-2, medium 4, near 8, tactical <= 16 | yes with the 0.22.5 cap of 16 (was 32); the dashboard knob `sea_tess_level_max` raises it after profiling |
| Gerstner or FFT | Gerstner, 10 waves per set, two sets cross-fading on a wind change (`common/waves.glsl`) |
| Normal maps | yes, two scrolling samples of the Meshy ripple normal (0.22.4 tile) |
| Fresnel | yes, with dispersion |
| Sun glitter | yes, GGX lobe + sparkle term with specular AA |
| Foam | yes: whitecaps (Jacobian + slope, masked by the foam tile), surf, lapping line, cliff burst, ship foam |
| Ship wakes | bow wave + hull heap in the vertex stages; no trailing V wake (by design, 0.19) |
| Reflections | sky and sun only; no planar / SSR of the hulls - **the open item** |
| Wind / storm / depth influence | yes (wave set from the wind cell, storm amplitude, depth gate + shoaling) |
| Frustum culling, cap, cracks, morphing | yes / yes / shared edge levels / fractional_odd spacing and the Nyquist fade |
| Share wave function with physics | the CPU `Sea::height_at` evaluates the same sum (the camera floor uses it); the ships ride it in the mesh shader |

Open from issue2: *riggings are wrong* still needs a sentence on what is wrong; *waves larger
than islands* still ambiguous; the trailing wake and the hull reflections are the two visible
sea items left.

## Session notes (2026-10-31 midday, 0.22.4)

- **Mirrors.** `/home/.../.cache/claude-hfi/-mnt-c-projects-mare/` is plain WSL storage that a
  background hook copies to `C:\projects\mare_<name>` with a lag; the alias of this morning's
  mirror vanished mid-session. Create the mirror *directly* under `/mnt/c/projects/` from now
  on: this session's is `C:\projects\mare_oak_20261031_120901` with its own copy of
  `assets\elm` (854 MB), `assets\data_chunks_terrain` (2.1 GB) and the sea sources
  (`assets\sea_src`), and `shot.bat` / `rig.bat` / `mask.bat` / `modeshot.bat` point
  `MARE_ASSETS` and `MARE_GEOCHUNK_DIR` at them. Nothing in the shared `C:\projects\mare\assets`
  is written by this session.
- The world package registers as four datasets (LOD 0-3). `dalmatia-detail-3x` is not in it;
  it still sits in `C:\projects\mare-world-terrain\data` if a finer Dalmatia is wanted.

## The issues folder, triaged (assets/issues/issues-1.txt, 2026-10-31 11:53; 0.22.4)

| Item | Status |
|---|---|
| cog mast too thick and 1 m above deck | done: `MAST_SLIM` 0.44 -> 0.34, `MAST_STEP` 0.14 -> 0.20 |
| all masts too thick | done (same) |
| lantina not equal thickness / "part of mast taken" / floating | the `lateen_yard` asset is a clean 45-degree spar (15k verts, none more than 4% off its axis - checked), slung at its middle on the mast; what reads as unequal ends is the scan's taper. Procedural one is one switch away (`shipyard.cc` lateen branch) |
| cloth goes through ship / aft sails need raising | done: aft stations of 3-4 masters lifted `CASTLE_LIFT` 0.10 hull heights (mast, lantina, canvas) |
| add wind into sails | done: canvas belly 0.52 -> 0.62, fuller floor |
| riggings are wrong | not touched: needs a sentence on what is wrong (shroud count, stays, ratlines?) |
| round islands in Greece / Caribbean / Peljesac | done: 5x5 Gaussian coast rounding + the data's own waterline inside the band + the mask follows the data (`MARE_TERRAIN_ISLANDS` default on) |
| proper textures everywhere incl. tessellation | sea: the three Meshy tiles installed (tools/sea_tiles.py). Terrain atlas unchanged |
| React UI | no - the UI is cimgui in C; a web UI is a different product decision |
| rivers 3D depth and flowing water for ships | waiting for the river data (user); carving is behind `MARE_RIVER_CARVE=1` |
| sea more reflective of ships / more transparent in battle view | partly: staged scenes Fresnel cap +0.17, alpha 0.72. A *reflection* of the hulls needs a planar reflection pass (not done) |
| waves in strategic view larger than islands | not understood - larger *than* islands? see question below |
| ship moves by wind, backwards head-to-wind | done, without the astern part: point-of-sail curve in `wind.cc` (quarter 1.20, run 0.95, beam 0.89, close-hauled falling to 0.18 in the eye). Going astern would strand AI routes |
| ship bow waves in 3D | exists since 0.19 (`ship_wave` in the sea vertex stages) |
| circle ending the view (lighting) | done: every close-range terrain fade gated by `u_camdist` as well as fragment distance |
| close-by camera shaking | done: the eye floor is smoothed and stands on the swell amplitude, not the crest under the eye |
| "where are real ship assets" | the authored Meshy fleet is what draws at battle and harbour zoom; from the regional zoom the LOD rules draw the procedural hulls (`MARE_LOD_BIAS`), and `MARE_SHIPS=0` would hide them entirely - check the dashboard LOD tab |

Question back: "waves in strategic view should be larger than islands" - larger wavelength (one swell line across a bay) or bigger foam? Either is a knob (`sea_zoom_amp`, `u_capzoom`).

## Session notes (2026-10-31 late morning, 0.22.3 - the 0.22.2 regressions undone)

- **What was wrong with 0.22.2 / 0.22.1**, as seen from the screenshots: the river channels
  (ship-scale beds below sea level) and the canyons cut gorges through the Po plain and every
  range; the world at 17 units flattened the Alps to brown hills; the warm-sea ramp turned
  every Adriatic shallow turquoise and the surf band (to 1.8 units of depth = the whole shelf)
  striped the bays white; the lantina had gone procedural. All reverted or rebuilt - see the
  CHANGELOG. The restructure (file renames) and the roads are kept.
- **Rivers are frozen** until the authored river data arrives: painted hairlines, no carving,
  no navigation (`MARE_RIVER_CARVE=1` to compare with the old channels).
- **Peljesac / Ston** are authored by hand in `geo_coast.h`; `mare.exe --mask=lon0,lat1,w,h`
  prints the land mask and the folded base height as ASCII, which is how to check a coast
  edit without a screenshot. Remember the rings round away any one-cell land strip.
- **The hinterland** is a fourth ring texture (`landuse`) + shader patterns + `gfx/scatter.cc`
  props. Thresholds are against the *drawn* slopes (the export normal is exaggerated ~15x).
  Open: the patterns and the props are at different scales (KNOWN_ISSUES), the parcels ignore
  the road direction, no bridges, no road upgrade visuals, no stone walls (items 10b, 6b, 9a).
- **The shared `C:\projects\mare\assets\data_new` and `data_new_2` vanished at 11:19** while
  this session was running (the other session is reorganising the asset tree - `assets/issues`
  and `assets/sea` changed in the same minutes), so every run after that fell back to the
  procedural world ("no terrain dataset found", land max 11.67 instead of 18.38). The datasets
  still exist in the exporter's folder, `C:\projects\mare-world-terrain\data\` (world-overview-v2,
  export-global, export-med, dalmatia-detail-3x, world-game-package); this session's bats point
  `MARE_GEOCHUNK_DIR` there. **Check the "terrain-v2 dataset" log line before trusting any
  terrain screenshot.**
- **Process.** A previous turn's agent (session aa20aeca, step 4) was still alive in this
  worktree when this session started, building and screenshotting from
  `C:\projects\mare_oak_20261031_103355` and writing `.build_mirror` files here; it was
  interrupted (SIGINT) and its mirror removed. This session's mirror is
  `C:\projects\mare_oak_20261031_104331` (`tools-bat\build.bat`, `shot.bat lon lat dist yaw
  file secs [tilt]`, `rig.bat dist yaw file`, `mask.bat lon0,lat1,w,h`). Old oak folders
  (`/tmp/mare_tools`, `C:\projects\mare\tmp\oak-*`, `*20tools`) are gone; the elm session's
  (`mare-elm-*`, `/tmp/mare_mirror*`, `/tmp/mare_stamp`) were not touched.

## Session notes (2026-10-31 morning, 0.22.2 - restructure, canyons, roads)

- **Every `C:\projects\mare-oak-*` mirror was deleted externally** mid-session (s21, s22 and
  this session's 9f81e43c), while `mare-elm-20261031-093630` appeared - another worktree's
  session is sweeping the shared folder. Builds done against a vanished mirror *still report
  success* (Windows kept the directory open) on stale sources. The mirror is now
  `C:\projects\mare_oak_20261031_093655` (datestamped, matching the snapshot) and every
  build is preceded by a Windows-side `findstr` on the file just edited.
- **Snapshots** of the committed tree (git history + exported assets, no `tmp/`):
  `/home/nevenjovi/.cache/claude-hfi/-mnt-c-projects-mare/oak_20261031_093655` (before the rename, commit f04b142) and
  a second one after this round (see the commit message for its stamp).
- **Rename map (0.22.2):** `v3.c/.h` -> `game/src/platform/`, `v3.sh` -> `tools/v3_framework.sh`,
  `game/mare.cc` -> `game/src/main.cc`, `world/geochunks` -> `world/terrain_chunks`,
  `world/terrainmap` -> `world/terrain_overview`, `gfx/streamview.cc` -> `gfx/terrain_rings.cc`,
  `gfx/chunkatlas` -> `gfx/terrain_atlas`, `gfx/detailview` -> `gfx/staged_scene`,
  `terrain/detail.glsl` -> `terrain/rings.glsl`. **Symbol prefixes were kept** (`geochunks_*`,
  `streamview_*`, `detailview_*`): they are a C ABI used from forty files and renaming them
  is churn without a reader's benefit; the file name now says what the module is.

## Session notes (2026-10-31, 0.22.1 — bug round)

- **The build mirror is `/mnt/c/projects/mare-oak-9f81e43c`** and must be rsynced to *at that
  path*. The WSL-side `.cache/.../elm-oak-9f81e43c` alias is not the mirror - writing there
  silently builds stale sources. Verify with `grep` on the mirrored file before trusting a
  build, and `taskkill /F /IM mare.exe` first: a leftover run holds `mare.exe` and the link
  fails with LNK1104.
- The worktree's own `assets/` is a stale partial copy. The live asset tree the game reads is
  `C:\projects\mare\assets` (`MARE_ASSETS` points at `elm` inside it, the terrain packages
  sit beside it). Do not delete from the worktree copy expecting the game to notice.
- **Still open from this round's request**, not attempted: "proper textures and materials like
  sea (tessellation, snip one with borders)" - too ambiguous to guess at; needs a sentence on
  which surface and what the border should do.
- Ships are *allowed* into the navigable channels (`navriver`, `nav_river_limit`) and the
  channels are now real water, but no fleet has been watched sailing one end to end. The nav
  grid's cell is 4 world units against a 2.1-unit channel, so a hull can path off-centre and
  clip a bank - see KNOWN_ISSUES.

## Session notes (2026-10-31, 0.22, second pass)

- **Build mirror** is now `C:\projects\mare-oak-9f81e43c` (written at `/mnt/c/projects/...`
  directly: the WSL cache path is not visible to Windows). `tools-bat\build.bat [test]`,
  `shot.bat lon lat dist yaw file secs`, `sunshot.bat lon lat dist yaw file sx sy sz tilt`,
  `modeshot.bat lon lat dist yaw file mode`.
- The second delivery (`assets/data_new_2/world-game-package`) is the LOD package; the
  `dalmatia-detail-3x` set its guide mentions is **not in the folder yet** - the reader
  accepts 257-sample chunks, so it will load when it lands (check the log for a
  "terrain-v2 dataset: 3x3 chunks of 257 samples" line).
- The GL debug warning about a shadow sampler bound to a non-depth texture is pre-existing
  (the 0.21 archived build prints it too) - recorded in KNOWN_ISSUES.

## Session notes (2026-10-31, 0.22)

- **Build loop.** The shared mirror `C:\projects\mare-oak-s21` was used by another session
  mid-run (a `mare.exe` with a random seed appeared in its logs; the next runs died after two
  frames), so this session moved to a private mirror `C:\projects\mare-oak-s22`: rsync
  `game/ tools/ ext/ v3.*` from the worktree, `cmd.exe /c C:\projects\mare-oak-s22\tools-bat\build.bat`
  (`test` for the headless suites), `tools-bat\s22.bat <lon> <lat> <dist> <yaw> <shot.ppm> <secs>`
  for pinned-camera screenshots (cmd splits on commas: the camera goes in as four arguments),
  `python3 -I tools/ppm2png.py` to look at them. The old memory note about `tools/winbuild.sh`
  is wrong: no such script ever existed.
- **Measured.** Overview dataset found beside the assets (27x14 chunks, 2795 m/sample, wraps),
  bake 3,915,031 cells in 149-297 ms, chunk atlas 48 MB, near ring 262,144 / 262,144 texels
  from the chunks with maps once resident, far ring 448,756 at the Dalmatian coast. Headless
  suites (diag, jobs, sim, asset, entity) green on MSVC.
- **Leftover.** Regional datasets' material maps are not in the atlas yet; the far ring's
  border shows the raster coast at the regional zoom (KNOWN_ISSUES); a camera pinned over a
  port drops into the staged city scene, so screenshot cameras should sit off the harbour.

# Session summary (2026-10-30 night, worktree `oak`, 0.21.0)

- **Wrong GPU, found by measuring**: the executable did not ask for the discrete chip, so GL
  ran on the Intel iGPU. Two exported symbols later: 52.7 -> 202.7 fps (3.85x).
- **Vulkan renders**: real SDK headers, loader at run time, transfer *and* render self tests
  passing (SPIR-V pipeline -> offscreen triangle -> verified pixels) on an RTX 5070 Ti.
- **DX12 bring-up**: DXGI adapter, device at feature level 12.2, direct command queue.
- **Options panel** with sliders, persisted to `mare_options.cfg`, verified by A/B on the
  metrics (shadow radius 113 vs 452; draw calls 93 vs 111 with tessellation).
- **geochunks finished**: the geography fold reads the float32 chunks (3,925,398 cells vs the
  cut's 2,205,872) and runs on the job pool, 1019 -> 251 ms.
- **Classes**: Tables, Json, Math/Geo/Rng/Calendar/Colour, EntityRegistry, Options,
  BackendSelector - the header-only modules now have one definition instead of forty.

## Process note, worth remembering

`/tmp/mare_tools/mirror.sh` was **overwritten by another session** mid-run, so it silently
synced a different worktree to a different folder. Several builds and test runs in this
session were therefore against stale sources before I noticed, including a "no change" result
for the GPU hint that was simply never compiled. The mirror is now
`C:\projects\mare-oak-s21` with the rsync written inline per command rather than in a shared
script, and every batch helper lives under that folder. **Verify the mirror received the edit
before trusting a build result** - `grep` the mirrored file, it costs nothing.

## Leftover tasks (0.21)

- [ ] Vulkan/DX12 draw nothing yet - see KNOWN_ISSUES. The next step is one real pass on
      Vulkan, which needs the GLSL library compiled to SPIR-V as a build step.
- [ ] A finer DEM is still the only thing that will sharpen the near coast.
- [ ] T9's remainder: the C-ABI globals and their shims.
- [ ] The islands `raise_small_islands` lifts still read as flat-topped plateaux.

# Session summary (2026-10-30 late, worktree `oak`, 0.20.0)

- **`assets/data/how_to.txt` implemented as written** (`world/terrain_chunks.h/.cc`): the export is
  read as 513x513 float32-metre chunks with `coverage.r8`, bounds from the manifest checked
  against a shipped `tile.json`, streamed IO -> MAIN under a 96 MB budget, and preferred over
  the u16 cut in the clipmap rings. Measured source split on the Adriatic: near ring 262144
  texels from the fine set, far ring 80250 from the chunks + 111372 from the cut.
- **Height atlas** (`gfx/heightatlas.h/.cc`, `terrain/atlas.glsl`): resident fine tiles on the
  GPU, sampled by the vertex/tess stages for the sub-ring residual, clamped so it can never
  cross the waterline. It probes the data first and **disables itself**: the residual is
  0.0016 rms / 0.031 peak world units against relief of 0.7-4.1, because at WORLD_K 16 the
  ring texel (~1.1 km) is already within 1.8x of the fine set's 620 m samples.
- **Vulkan step two**: memory types, buffers, mapping, command pool/buffer, queue submit,
  fence wait and a checked 64 KB GPU transfer - passed on an RTX 5070 Ti in 0.11 ms.
- **No terrain geometry shader**, deliberately: at 28 km per world unit, scatter geometry is
  meaningless. Reasoning recorded in DESIGN_RENDERER.md 0.20.
- Build loop moved to a **sources-only mirror** (C:\projects\mare\tmp\oak-s20, 15 s) with
  `MARE_ASSETS` pointing at the main checkout's asset tree - another session deleted
  C:\projects\mare\tmp mid-session and the worktree's `tmp` symlink flipped to a local dir.

## Leftover tasks (0.20)

- [ ] **A finer DEM is the only thing that will sharpen the near coast** - the renderer is
      already Nyquist-matched to the data (DESIGN_TERRAIN.md 3d). EU-DEM + EMODnet for the
      Adriatic, cut into `height_fine`, would turn the height atlas on automatically.
- [ ] Vulkan pipelines need SPIR-V: install the Vulkan SDK or glslang on the build machine.
- [ ] The islands `raise_small_islands` lifts still read as flat-topped plateaux.
- [ ] `geochunks` is not used by the one-shot geography bake (that still reads the u16 cut);
      harmless, but the bake would be exact too if it read the chunks.

# Session summary (2026-10-30 night, worktree `oak`, 0.19.0)

- **The issue screenshot (assets/issue-1.png)**: the "lantinas in the wrong place" were the
  rig's *ropes* - fixed-width quads that came out 37 cm thick at the staged battle's scale and
  read as black spars; the "stock lantinas" were the sails' procedural spars. Ropes are now
  line segments a geometry stage widens to their physical size (0.6 px floor); every sail
  hangs from the authored lantina / split square yard instanced on its own swung heading,
  with the procedural spar only past 44 px (gfx/shipyard.cc).
- **Flags** are verlet cloth per ship, pinned to the main masthead, in real units for the
  class; **materials** for canvas (seams, tablings, weave, transmission, sheen), water (crest
  scattering) and ground (per-material roughness, micro relief).
- **Coast**: the clipmap rings move the shore inside a band around the raster waterline -
  authored GEBCO heights where resident, blurred raster corners and a texel-aware wiggle
  elsewhere, harbours protected - and the sea samples the rings, so the waterline follows.
- **RHI**: `rhi::Device` + `GlDevice`, `rhi.cc` shims, `--rhi=`; Vulkan bring-up device (loader,
  instance, device, queue, adapter report; draws via GL); DX12 stub.
- Verified on screen: staged battle wide and close (hemp, lantinas, flags), Dalmatian coast
  close-ups, `--rhi=vulkan` run; headless suites green.

## Leftover tasks (0.19)

- [x] Yard LOD chains: re-exported with `tmp/s19tools/yardlods.bat` (Blender 5.2, budgets
      60k/20k/6k/2k/700/250) into both asset trees; descriptors regenerated. The far levels
      are slivers, so the sail's own spar still takes over below 44 px.
- [ ] Dalmatian walls: the plateau behind the coast at 16.45E 43.5N is GEBCO's own Dinaric
      relief (the ring's debug line shows the near ring lowering nothing there), cut vertically
      by the 7 km raster at the waterline and shown with the world's 2-3x vertical exaggeration.
      The sea-side shore band now lets the mountain foot continue where GEBCO says land; the
      islands lifted by raise_small_islands keep a plateau profile. Next levers: a beach
      profile for lifted islands, a lower height scale near the coast, and a finer raster.
- [ ] Vulkan T8: SPIR-V, swapchain, pipelines, uniform blocks (see REWRITE_PLAN).
- [ ] The GL context runs on the Intel iGPU while Vulkan picked the RTX: an adapter preference
      for the GL window (NvOptimusEnablement export) would move the whole game to the dGPU.
- [ ] Gunsmoke puffs still read faceted very close (visible beside the flags in the battle
      wide shot); the sphere needs more segments or a billboard.

# Session summary (2026-10-30 evening, worktree `oak`)

- **Fine terrain + tessellation**: new `height_fine` Mediterranean set (19x8 tiles, ~520 m,
  cut from the mare-world-terrain GEBCO export) layered over the global base; terrain chunks
  tessellate on GL 4.0 (8x8 patch lattices, up to 32/edge, watertight; MARE_TERRAIN_TESS=0
  reverts); TT_HEIGHT_SCALE 28 -> 20 (lower overall height, detail kept). The blocky coast
  silhouette at battle zoom is the old landmask raster, not the new data (see Leftover).
- **Rig to the reference sheet**: lantina re-proportioned to assets/fleet_sails.png (long,
  low at the bow, peak past the masthead), trims up to ~70 degrees to the apparent wind;
  bellies 1.5x fuller downwind; canvas blown aback clings to mast and spar and shivers.
- **Waves**: squared shallow damping (shelves nearly calm), strategic swell only in the
  zoomed-in band at <= 0.07 amplitude, zero past it.
- **Builds archived** to tmp/builds/<root>-<datetime> on every successful build.
- Verified on screen (battle, galleys, Dalmatia close, strategic x2, shallow bay) and all
  headless suites green.

# TODO (session of 2026-10-30 afternoon, worktree `oak`)

- [x] **Real terrain wired in**: `assets/data/export-global` (GEBCO, 378 chunks of 513x513
      float32 metres - see `assets/data/how_to.txt`) cut by the new
      `tools/terrain_from_gebco.py` into the streamed tile set (40 samples/deg over the
      playable window, land linear to 9000 m, sea sqrt-compressed to 12000 m so shelves stay
      in the band the shallow shading expects). Installed at `assets/elm/terrain/tiles`; the
      AI-map cut is kept in `tiles-aimap`, `MARE_TERRAIN_DIR` switches cuts for A/B. This
      closes the "a real DEM" item below; the *coastline* still comes from data/geo_coast.h
      (GEBCO is ~3 km/sample - too coarse for the Adriatic islands; MARE_TERRAIN_ISLANDS=1
      remains the experiment).
- [x] **Sea floor darkens exponentially** past the shelf (sea.frag): basins and trenches now
      read against the shelf edge instead of saturating at the old `deep` colour.
- [x] **Ship wood material**: grain-driven plank albedo + strake streaks + gloss on rough
      brown surfaces, and a wet, darker, glossier band above the waterline on floating
      instances (meshes.vert/.frag).
- [x] **New mast library** (assets/ships/new_masts, explanation.txt): per-type assemblies
      (mast_cog_single/_two, mast_carrack, mast_great_carrack, mast_mizzen, mast_lateen_tall,
      bowsprit) imported via tools/blender_prepare_part.py (part-segmented GLBs: lateen yards
      excluded from their masts); classes compose their stations from the library with
      fallback to the old split parts. The **lantina swings to the wind**: `lateen_yard` is
      its own asset, instanced with the (shared unit) lateen canvas on the mast pivot,
      trimmed off the apparent wind with a working oscillation (shipyard.c).
- [x] **Lantina visible again at every distance**: the authored lateen_yard / square_yard
      assets lost their far LODs to decimation (a thin diagonal spar decimates to a sliver),
      so the yards vanished beyond ~30 px. The spars are now *part of the unit sail meshes*
      (rig_sail_lateen / rig_sail_square build their own bar, sized to the sail), so yard and
      canvas swing to the wind as one mesh and exist at every LOD; the spar-asset instancing
      is retired (the assets stay on disk, unused). Canvas-to-yard alignment is now exact by
      construction.

## Leftover tasks

- [ ] Bowsprit steeve: the authored spar is horizontal; bake a ~18 degree tilt about the heel
      in blender_prepare_part.py (instances have yaw only) and re-export.
- [ ] Far-LOD budgets for thin-spar *masts* (the "LOD extent differs" warnings): raise the
      LOD4/5 triangle budgets for the mast library or keep LOD3 longer in their descriptors.
- [ ] A floating yard/bowsprit was once seen at water level beside a wake in a staged battle:
      likely a part instanced while its hull was still streaming (shipyard_push draws parts
      only after the hull resolves, so probably the occlusion-query partition) - reproduce
      and pin down.
- [ ] The asset loader fits a `forward: +z` part *after* rotation, so square_yard reported
      size 0.9x1.17x16.7 with scale 17.96 (harmless while shipyard rescaled by bbox; the
      asset is unused now, but the fit-vs-rotation order in asset.cc deserves a look).
- [ ] Retire or repurpose the unused spar assets (lateen_yard, square_yard) and the parallel
      session's whole-assembly imports (carrack_mast, mizzen_mast, ...) from the manifest.
- [ ] Rewrite plan T4/T8: the abstract rhi::Device interface over GlDevice, then the Vulkan
      backend (docs/REWRITE_PLAN.md); T9 folder sweep (physical src/ tree per namespace) and
      the RmlUi decision (T7) after it.
- [ ] Deepen the conversion: the classes still reach the shared C globals (G, RF, cam);
      migrating callers onto the class APIs and injecting game_t is the next honest step.
- [ ] Session coordination: oak/tmp keeps flipping between a symlink to C:\projects\mare\tmp
      and a local directory (two sessions, one worktree); builds now address
      /mnt/c/projects/mare/tmp/elm-cpp explicitly, but the flip-flop should stop.
- [ ] A parallel session imported the same sources whole (carrack_mast, mizzen_mast, ... with
      .meshy.json markers) into the shared assets/elm; both sets coexist in the manifest.

# TODO (sessions of 2026-10-29, worktree `elm` — branded `oak` in the first session)

Reference art for the rigs: `assets/fleet_sails.png` (the whole fleet, one ship per class) and
`assets/fleet_sails_2.png` (war carrack and great carrack: four masts, spritsail, lateen
mizzens; ignore the hull distortion). Both live in `C:\projects\mare\assets`.

## Part 1 — done (0.15.0)

- [x] Masts back (split-part descriptors regenerated into the manifest).
- [x] Rig layout per class from the reference sheet (`gfx/shipyard.c` class table), four-mast
      war/great carrack, spritsails, drawn yards.
- [x] Cloth that answers the wind (`GLSL_CLOTH` in `gfx/meshes.c`): belly from wind pressure,
      bent to the yard, held at the foot, luffing, aback against the mast; flags stream.
- [x] Per-class ship stats (`ship_stats_t`, `shipyard_class_stats()`); the staged battle sails
      at class speed and turn rate.
- [x] Ship bow waves and foam only where the ship is going (`gfx/sea.c`, `MARE_SHIP_WAVES=0`).

## Part 2 — done (0.16.0)

- [x] Photos and tests: battle, city, Adriatic/Aegean/strategic terrain, oars, formations —
      `C:\projects\mare\tmp\elm-fin\shots`; keepers in `docs/screenshots/` (islands-aegean,
      islands-dalmatia, battle-lines). All five ctest suites green (sim_test caught — and
      drove the fix for — islands thinner than the nav grid).
- [x] The dark lattice on the staged sea: isolated to SSAO reading the tessellated sea's
      patch-border derivatives (A/B matrix MARE_SSAO / MARE_SEA_TESS / --no-shadows; edge-step
      metric 46.8k → 4.2k). Fixed by taking the AO pass from the opaque depth before the water
      draws; the surviving diagonal streaks are the rigs' real sun shadows.
- [x] Buoyancy: displacement-derived wave response + roll + deeper waterline per class
      (`shipyard.c`), inherited by masts, rig and oars.
- [x] Battle formations: line ahead on mirrored stadium circuits, station keeping, turning in
      succession, inner legs held a cable apart for passing broadsides (`staged_scene.cc`).
- [x] Oars: side offset reduced, sweep-rate stroke, blade dip on the pull, galley stats eased.
- [x] Strategic waves: bigger in open sea (zoom amp floor 0.14–0.26), damped near every coast
      by a zoom-driven depth gate (`u_shallow`).
- [x] Every island of the Adriatic and the Greek seas: 25 authored island polygons in
      `data/geo_coast.h` (plus a fuller Crete), each ≥2 cells so navigation respects it;
      small land components lifted clear of the water (`raise_small_islands`). The 32768
      height map is folded in for relief and bathymetry only (default on,
      `MARE_TERRAIN_GEO=0` reverts) — it proved to be a loose AI Earth whose own Adriatic is
      half filled in, so its *coastline* stays an experiment (`MARE_TERRAIN_ISLANDS=1`).
- [x] Per-worktree assets: `<root>/assets/elm` and `C:\projects\mare\assets\elm` carry the
      complete generated set (ships, mast parts, oar, buildings, all six LODs, descriptors,
      manifest, sea textures, terrain maps + tiles). The asset root resolves to `assets/elm`
      before `game/assets`; terrain tiles resolve per-worktree first. Builds in `tmp/elm-fin`.

## Part 3 — done (0.17.0)

- [x] World 2x again (WORLD_SCALE 4) with every gameplay distance expressed in the scale; base
      grid 2x (16 cells/degree) with nav/territory/stream geography unchanged.
- [x] Regions: cap 250, 3.2-degree coast segments in the Mediterranean home seas, Istria /
      Dalmatia / Ragusa and 25 more Mediterranean name pins.
- [x] Relief: ridged domain-warped mountains, hills everywhere inland, cliff coasts, a shelf;
      ridged streamed chunk detail; authored tiles add relief only where higher.
- [x] Textured ground from the material atlas (triplanar rock cliffs, scrub, snow, deserts),
      wet rock at the waterline, darker sea floor, warm turquoise shallows.
- [x] Found and fixed the vanished sea (AO pass left its framebuffer bound after the 0.16
      reorder) and three reversed-edge smoothsteps.

## Still open

- [ ] **A real DEM and coastline** (the one thing the terrain still needs): a 16-bit GEBCO /
      ETOPO equirectangular export plus a Natural Earth land-mask raster, dropped into
      `assets/elm/terrain`. The cutter, calibration, bake and rings are all in place; the AI
      map only ever supplies relief. See DESIGN_TERRAIN.md "What would make it real".
- [ ] Mountains are still modest against the reference art; the next lever is erosion (thermal
      / hydraulic passes on the ridged field) and a snow/rock crest treatment in the shader.
- [ ] `MARE_PASSES=sea` with `MARE_SEA_DEBUG=1` dies on an incomplete render target (the
      refraction snapshot with no opaque pass before it) - a debug-only path.
- [ ] The Great Carrack hull is still the war carrack's model stretched ×1.19: generating a
      true 56 m hull (and a four-mast rig to split) needs a new Meshy generation — external.
      The bonaventure reuses the mizzen part deliberately: k-means-splitting the three-mast
      scan into four would cut the mainmast in half.
- [ ] The overhead cloud layer shows hard polygon edges in the staged battle's top-down views
      (visible in battle-lines.png); gunsmoke puffs read faceted at very close range.
- [ ] Normal-map tiles are cut and loadable (`terraintiles_normal`) but nothing consumes them:
      ring lighting differentiates the composed height delta instead. Wire them into the rings
      or drop the cut to save 300 MB.
- [ ] The shared `assets/terrain/tiles` folder is contested between sessions (its height.json
      was overwritten mid-session by the other worktree); everything here now reads the
      per-worktree copy, but the shared folder should eventually be retired.
