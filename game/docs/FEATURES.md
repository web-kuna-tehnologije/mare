# Implemented features

Status as of the current codebase. Each entry names the module that owns it.

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
- Heightmap terrain: 374 chunks, 5 LOD grids with skirts, frustum culling, biome/desert/tundra
  colouring, parchment map style with territories and coast outlines, fog.
- Animated sea from the heightmap (depth colour, foam, distance-faded waves, graticule in map view).
- Procedural ship meshes (hull loft, masts, billowed sails, nation stripe, flag) in 2 LODs + icon;
  procedural towns (houses by population, tower, fort walls, pier, flag); instanced rendering.
- Screen-sized SDF icons; RTS camera with smooth 3D→map morph; picking and labels.

## UI (`src/ui`)
- Main menu (nation choice/load), top bar (date, speed, treasury, income, toggles, error badge),
  Selection panel (fleet: ships, cargo, orders, automation, merge/split, market; port: info,
  build, shipyard, production, population & cultures, market), Fleets list, Chronicle (clickable
  positions), Stats & diplomacy (nations, wars, price trends, standing), Help, toasts, world labels.
- Commands: select, sail, dock, attack, besiege, blockade, patrol, colonise site, claim coast,
  set route, auto-trade, follow, cycle fleets, save/load, map toggle.

## Observability (`src/core/diag.c`, `src/ui/dashboard.c`)
- Six-level logging to console (coloured), per-run file, cross-run error file with banners and
  crash records, in-memory ring; sequence-numbered, timestamped, sim-day-stamped records.
- Metrics (counters with rates, gauges with min/max), profiler scopes with rolling stats, metrics
  CSV in dev mode, signal handlers.
- Dev dashboard (F3 / `--dev`): performance graph and scope tree, metrics, filtered log, system
  info, render-pass toggles, test buttons.

## Tooling
- Headless tests (`tests/sim_test.c`, `tests/diag_test.c`), balance runs (`--simulate`),
  cross-platform build scripts, CMake project, GitHub Actions matrix (Windows/Linux/macOS).
