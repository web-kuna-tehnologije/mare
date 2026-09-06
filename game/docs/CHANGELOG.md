# Changelog

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
