# Implemented features

Status as of the current codebase. Each entry names the module that owns it.

## Ships and asset import (`src/gfx/shipyard.c`, `tools/meshy_import.py`)
- Eight authored Generation I classes (coastal trader, cog, fusta, galiot, armed cog, hulk,
  carrack, war carrack) assembled from a hull, a mast assembly and - for the oared
  Mediterranean interceptors - a bank of 20-22 oar pairs, sized from the design sheets at
  1 m = 0.045 world units.
- A game ship type maps to a class by nation: Venice, the Ottomans and the pirates sail the
  oared fusta and galiot, the northern powers the hulk. Procedural hulls remain the fallback.
- Parts go through the normal authored-asset path, so they inherit LOD selection, cross-fades,
  residency, shadow casters and wave displacement. Rigs that a generator made longer than their
  own hull are scaled down to a 15% bowsprit overhang.
- `tools/meshy_import.py` imports Meshy download trees (name resolution, five LODs per kind,
  WSL path translation); `blender_export_glb.py` takes `.glb` sources and can straighten a
  model exported at an angle. LOD 0 is capped at 150k triangles and only uploaded when an
  instance picks it.
- `mare --map-chunk=level,tx,ty` writes the shaded-relief reference and placement brief for a
  streamed tile, so world chunks can be generated and imported as `tile_l<level>_<x>_<y>`.
- `MARE_SHIPS=0` falls back to the procedural fleet; the dashboard LOD tab reports authored
  hulls, oar instances and fallbacks.

## World streaming (`src/world/stream.c`, `src/gfx/streamview.c`)
- Tile hierarchy continent -> region -> coastal tile -> tactical chunk: a 4-level quadtree of
  512 tiles over the map (64° -> 8° per side), each a 257×257 delta heightfield against the
  base map, deterministic from the seed, faded out at coasts and over water (the simulation
  never reads tiles). 227 all-water tiles never load.
- Disk tile cache (`tiles/*.mht`) through the job system: IO-lane reads/writes, DECODE-lane
  generation, MAIN-lane state flips; 64 MB budget (`MARE_STREAM_MB`) with LRU unloading that
  spares the wanted set and resident parents.
- Terrain detail window: the finest resident tiles composed into one 512² texture around the
  camera (parallel, sub-ms) and added in the terrain vertex shader with a border fade.
- Progressive city loading: coarse-first LOD uploads + resident-LOD fallback in the scene,
  so towns walk icon -> LOD4 -> ... -> LOD0 without pops or upload stalls
  (`asset.lod_fallbacks`).
- Authored terrain tiles: a `tile_l<level>_<tx>_<ty>` asset replaces a tile's visuals; the
  dashboard Streaming tab and the world-space tile overlay (`MARE_STREAM_TILES=1`) list the
  grids still to model (docs/BLENDER_LOD.md).
- Tick phases on the worker pool: economy / settlements / fleet-speed pre-pass split into
  parallel compute + ordered commit (bit-identical, covered by sim_test); adaptive dispatch
  past 2048 entities because the pool wake costs more than today's whole phase
  (`MARE_PARSIM=0/1/2`). Headless `tests/stream_test.c` covers grids, determinism, the disk
  round-trip, the budget and the eviction policy.

## World (`src/world`, `src/data`)
- Hand-authored coastlines for Europe, Africa, North & South America, the Caribbean and Atlantic
  islands; inland seas/lakes (Black Sea, Great Lakes...) carved; 18 straits forced navigable.
- 1408×1040 land mask (8 cells/degree), signed coast-distance field, procedural heightfield
  (coast ramp + fbm mountains, polar flattening), territory map (nearest settled port influence).
- Navigation grid (704×520) with water-body labelling, A* (octile heuristic, coast penalty,
  no corner cutting, 400k-node cap), fine-grid line-of-sight string pulling, water sub-cell
  waypoint snapping, port-to-port Dijkstra distance rows cached per source port.
- Wind model: trades, westerlies, horse latitudes, doldrums, polar easterlies, seasonal drift,
  noise; ±30% speed effect by heading.
- 110 historical ports and colony sites with 1620 ownership, production tags, pirate havens,
  historical settlers and settlement years.

## Economy (`src/sim/economy.c`)
- 12 goods with base prices, per-capita consumption, luxury classes, regional demand profiles
  (colonies import manufactures, Europe imports colonial goods).
- Production = resource potential × labour × workforce, hinterland staples, buildings
  (Plantation, Mine, Docks), supply elasticity; consumption with price elasticity.
- Price curve from stock vs norm (bounded 0.25×–4×), pirate fence discount, surplus absorption.
- Trade with tariffs (home 2%, free city 3%, foreign 4%, havens 0) paid to the owner; war and
  blockades forbid trade. Player fleets spend the treasury; AI fleets carry their own capital and
  remit profits.
- Prosperity (needs met, traffic, market, output per head, blockade penalty), unrest,
  population growth capped by site capacity, fort regeneration, blockade expiry, taxes.
- Weekly world price history for the Stats panel.

## Settlements & colonisation (`src/sim/settlement.c`)
- Free colony placement on any valid coast; climate/terrain-derived resource potentials.
- Labour allocation over local goods (player +/−, AI drifts toward dear goods).
- Population capacity, immigration (owner + trading-partner cultures), assimilation by owner
  tolerance, culture-driven unrest (doubled at war), civil rebellion/secession to the majority's
  nation or independence.
- Merging of neighbouring founded settlements: larger absorbs smaller, sums stocks/cultures,
  keeps max buildings, ownership to the dominant culture, unrest shock, full reference remap.
- AI claims promising coasts near its colonies (from 1628) and settles historical sites on schedule.

## Hinterlands & trading hubs (`src/world/regions.c`, `src/sim/region.c`)
- ~160 land regions from coast segmentation + land BFS; named from pins or their main port.
- Region resources from climate plus rare deposits (iron, coal, copper, silver, gold; noise + pins).
- Hub ports (≤3/region) with dynamic shares; two-way throughput-limited links; rural production into
  export pools; rural import needs; development tiers; prosperity; inland mines/farms; inland sales
  tax and seigniorage for hub owners; region demand deepens hub markets; iron boosts arms output.
- Directed colonisation: attractiveness drives AI coast choice; site panel shows the hinterland;
  Regions overlay and inland icons in the map view; Hinterland section in the port panel.

## Fleets, AI & warfare (`src/sim/fleet.c`, `ai.c`, `combat.c`)
- 6 ship classes; fleets of up to 12 ships with cargo, morale, orders (move, dock, trade, attack,
  patrol, siege, blockade, colonize, flee, raid), wind/storm-modified movement, stable ids.
- Trade planner with lot sizes, saturation-aware revenue, tariffs, return-leg estimate,
  mercantilist preference, pirate-risk penalty; player automation: fixed routes and free
  auto-trade.
- Merchant, warship (patrol, hunt, opportunistic sieges with war-aim limits), pirate (hunt busy
  lanes, fence prizes), colonist and treasure-fleet behaviours; nation AI buys fleets, invests
  in buildings, colonises, spawns pirates; yearly Spanish treasure fleet.
- Combat: broadsides per day of contact, luck/morale, capture vs sinking, loot, bounties,
  morale retreat; merchants flee stronger hostiles; sieges with fort HP, fort return fire, storming
  requirement, capture cooldown; blockades.
- Emergent trade lanes (cached paths with decaying traffic) shown in map view.

## Events & diplomacy (`src/sim/events.c`)
- Storms/hurricanes (seasonal, moving, damaging), plague, bumper/failed harvests, silver strikes,
  fashion crazes, settler arrivals, fires, revolts, random wars and peaces, AI peace offers.
- Scripted 17th-century wars (1621–1678) including the player's nation; fleets expelled from ports
  that turn hostile; player diplomacy (declare war / sue for peace).

## Rendering (`src/gfx`)
- API-agnostic RHI (`rhi.h`): pipelines with baked depth/blend state, geometry objects with
  streamed instance buffers, format-enum textures. GL3 backend (`rhi_gl.c`) is the only game
  file calling GL; the backend include in `mare.c` is the switch point for Vulkan/DX12 later.
  Modernisation roadmap: docs/DESIGN_RENDERER.md.
- HDR: 3D passes render into an RGBA16F target, tonemapped to the backbuffer (`hdr.c`, exposure
  control); UI composites on top. `MARE_SCREENSHOT` captures the backbuffer for smoke tests.
- Linear/sRGB pipeline: sRGB author colours converted on input, lighting in linear, a single
  sRGB encode after tonemapping.
- PBR materials (GGX/Smith/Schlick + hemispheric ambient) with per-vertex roughness/metalness;
  one directional sun shared by every pass (`lighting.c`).
- Cascaded shadow maps: 3 cascades in one depth atlas, ground-footprint fit, texel snapping,
  normal-offset bias, 3x3 PCF. Ships, towns, forts, piers and flagpoles cast; terrain receives.
- Procedural normal maps (`detail.c`): two-scale scrolling ocean waves; triplanar surface grain
  on the UV-less meshes. Shallow water is transparent down to the seabed.
- Per-object frustum culling (ports, fleets) with shadow-caster margin; per-frame instance
  buffers shared by all passes; RHI draw/instance/upload statistics in the dashboard.
- Profiling: per-pass GPU timer queries (read back without stalling), RHI VRAM accounting and
  process RAM, in the dashboard Perf tab and the metrics CSV.
- Ship motion: smoothed render positions between simulation ticks, roll + pitch rocking scaled by
  speed and weather, translucent gunsmoke on firing broadsides.
- Heightmap terrain: 374 chunks, 5 LOD grids with skirts, frustum culling, biome/desert/tundra
  colouring, parchment map style with territories and coast outlines, fog.
- Animated sea from the heightmap (depth colour, foam, distance-faded waves, graticule in map view).
- Procedural ship meshes (hull loft, masts, billowed sails, nation stripe, flag) in 2 LODs + icon;
  procedural towns (houses by population, tower, fort walls, pier, flag); instanced rendering.
- Screen-sized SDF icons; RTS camera with smooth 3D→map morph; picking and labels.
- Occlusion culling: authored buildings hidden behind other buildings are tested with hardware
  occlusion queries against the depth buffer and left out of the main pass (shadows still
  cast); a terrain-horizon ray for towns and ships (DESIGN_VISUAL.md step 1).
- Wind-driven sea: five Gerstner waves built each frame from the wind at the look-at point -
  direction, spread, wavelength and height all follow it, from a glassy doldrums to a heavy
  westerly - with per-seed randomness, accumulated phase, downwind surface drift, and a
  grid-aware fade that keeps the swell from aliasing as the camera moves. Ships ride the same
  waves.
- Shadow cascades cast from a coarse per-asset mesh; anisotropic filtering at the driver
  maximum on every mipmapped texture.
- Tessellated sea (GL 4.0, warped-grid fallback) with the authored displacement map as
  geometric chop; foam from the wave Jacobian, surf in the shoaling band, bow and wake foam
  around nearby ships. Wave height follows the zoom (`sea_zoom_amp`: full at the water,
  flattening pulled out); ships and the camera floor ride the same scaled field.
- Weather: calm air leaves the sea glassy and hazy, dawn mist fills calm bays, storms dim the
  sun, grey the sky, thicken the fog and rain over the frame (`gfx/weather.c`).
- Water refracts the scene behind it with depth absorption and reflects the sky by Fresnel; an
  inner patch lattice keeps waves smooth at water level and the camera is held above them.
- Sky pass with sun disc, bloom and a wind-driven gradient-noise cloud layer that shadows the
  ground and is visible from above when zoomed out (`gfx/sky.c`); volumetric height fog with sun
  in-scattering; baked terrain horizon occlusion and screen-space ambient occlusion.
- Shadow cascades reach the mesh cull radius and take their casters from it, so towns drawn as
  icons or hidden behind buildings still cast onto the terrain.
- The simulation ticks once every 15 real seconds (`SIM_TICK_SECONDS`) rather than three times a
  second, advancing the accumulated time in one burst of fixed sub-steps; the world runs at the
  same speed and fleets are interpolated across the interval.

## UI (`src/ui`)
- Main menu (nation choice/load), top bar (date, speed, treasury, income, toggles, error badge),
  Selection panel (fleet: ships, cargo, orders, automation, merge/split, market; port: info,
  build, shipyard, production, population & cultures, market), Fleets list, Chronicle (clickable
  positions), Stats & diplomacy (nations, wars, price trends, standing), Help, toasts, world labels.
- Commands: select, sail, dock, attack, besiege, blockade, patrol, colonise site, claim coast,
  set route, auto-trade, follow, cycle fleets, save/load, map toggle.
- Save (F5) and load (F9) run on the IO lane: the frame pays a ~1 MB snapshot (0.1-0.3 ms), the
  file is written beside the old one and renamed over it when complete. Autosave every
  `AUTOSAVE_SECS` (180 s) of unpaused play to `mare_autosave.bin`, loadable from the menu.

## Observability (`src/core/diag.c`, `src/ui/dashboard.c`)
- Six-level logging to console (coloured), per-run file, cross-run error file with banners and
  crash records, in-memory ring; sequence-numbered, timestamped, sim-day-stamped records.
- Metrics (counters with rates, gauges with min/max), profiler scopes with rolling stats, metrics
  CSV in dev mode, signal handlers.
- Dev dashboard (F3 / `--dev`): Performance (frame graph, scope tree, GPU passes, memory),
  Metrics, **Jobs** (lane occupancy and queue depths with history, jobs/s, inline runs and
  blocked-wait time, the asset pipeline's stages in flight, save/load state and cost, autosave
  countdown), **LOD** (tier and budgets, the director's threshold scale, instance fade/pop
  counters, GPU residency per asset and LOD, streamed textures), Assets, filtered Log, System;
  `MARE_DASHTAB=` opens a tab for unattended runs.
- Every frame over 25 ms is logged at DEBUG with the previous frame's CPU/GPU split, upload
  jobs, assets loading and save/load state, so a hitch in a run log can be attributed.
- `tools/metrics_summary.py` turns a metrics CSV into a median / p99 / max table per column:
  the format the ROADMAP's before/after captures use.

## Concurrency (`src/core/jobs.h`, `jobs.cpp`)
- Thread pool of `cores - 1` workers over four lanes: WORKER (frame-critical, served first),
  DECODE (parsing, texture decode, mesh processing), IO (dedicated threads for blocking file
  work), MAIN (GPU uploads, only ever run by the render thread, budgeted per frame).
- Join counters, `jobs_parallel_for` with worker-count-independent chunks (so parallel
  reductions stay bit-identical), waits that help instead of idling, per-lane statistics.
- `MARE_JOBS=0` forces synchronous execution, `MARE_JOBS=N` pins the worker count.
- Clients: asset loading (IO → DECODE → MAIN), streamed textures, the port-distance precompute
  at game start (`parallel_for` on WORKER), save/load (`game_save_async` / `game_load_async`:
  snapshot on the main thread, file work on IO, result polled once per frame).

## Assets and LOD (`src/assets`, `src/gfx/lod.c`, `src/gfx/textures.c`)
- glTF/GLB asset runtime with JSON descriptors (scale fit, pivot, forward axis, material rules,
  texture baking, collision footprint); loads in the background on the job system, GPU upload
  budgeted per frame; `asset_generation()` lets caches rebuild when an asset lands.
- Screen-space LOD for authored assets, ships and towns: projected pixel size, thresholds in
  reference pixels (`px`) or authored distances, per-instance hysteresis and 0.3 s dithered
  cross-fades (meshes and shadows), mesh-to-icon dissolves; `lod.pops` / `lod.fading` metrics,
  `MARE_ZOOM` scripted zoom for measuring.
- View tiers (city / battle / regional / strategic / continental) with a triangle budget each;
  the director scales the thresholds when the frame is over budget so the smallest instances
  hand over a level first. Icons cluster at continental zoom. Residency: every asset is wanted
  up to the regional tier, beyond it the coldest assets are evicted once the GPU budget is
  exceeded and reloaded through the pipeline on return (towns draw their procedural fallback
  meanwhile).
- Authored sea tiles (seamless normal map + foam albedo cut from the reference sheet by
  `tools/sea_textures.py`), streamed; the procedural map covers until they are resident.
- Meshy building set: `tools/blender_export_glb.py` exports per-LOD GLBs (80k/24k/7k/2k
  triangles, 1k JPEG base colour only), `tools/gen_asset_descriptors.py` writes descriptors and
  the manifest; towns and the staged city place the houses by tier, plus warehouse,
  lighthouse and town hall when present.

## Tooling
- Headless tests (`tests/sim_test.c`, `tests/diag_test.c`, `tests/asset_test.c`,
  `tests/jobs_test.cpp`), balance runs (`--simulate`), cross-platform build scripts, CMake
  project, GitHub Actions matrix (Windows/Linux/macOS).
