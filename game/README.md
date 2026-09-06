# MARE LIBERUM

*A real-time strategy game of trade and war on the oceans of the 17th century, built in C on the v3 framework.*

You are an admiral of one of the maritime powers of 1620. Buy cheap, sail far, sell dear. Found colonies, fortify your
ports, hunt pirates, blockade your rivals and storm their cities. The world is a living economy: every port produces,
consumes and prices its goods every day; a hundred AI merchants chase the same profits you do, so trade lanes emerge,
saturate and shift on their own; storms, plagues, harvests, crazes, silver strikes, revolts and wars keep the map moving.

```
./game/build.sh          # linux / macos  -> ./mare
./game/build.sh test     # headless simulation tests (no window, no v3 needed)
game\build.bat           # windows (Visual Studio 2019+) -> mare.exe ; game\build.bat debug ; game\build.bat test
cmake -S game -B build && cmake --build build --config Release && ctest --test-dir build -C Release   # any IDE
```

## Platforms

Development happens on Linux; Windows and macOS are first-class targets and `.github/workflows/build.yml` builds and
runs the tests on all three. Everything is plain C11 with no platform code outside `src/core/diag.c` (timers, console
colours, directories) — the framework (v3/GLFW/microui) covers windowing, GL and input.

**Windows specifics**
* Build with `game\build.bat` (finds `vcvars` for VS 2019/2022 automatically) or open the folder in Visual Studio via
  `game/CMakeLists.txt`. Flags: `/std:c11 /utf-8 /MT`, Windows macro hygiene (`NOMINMAX`, `WIN32_LEAN_AND_MEAN`).
* Logs go to `logs\` next to `mare.exe`; saves to `mare_save.bin` next to it. The console shows colours on Windows 10+
  (virtual-terminal mode is enabled by the logger; plain text otherwise).
* The Windows default stack is 1 MB: the ~1 MB `game_t` state is only ever static/heap allocated (tests included).
* Identifier hygiene: `MAX_PATH` is a Windows macro, so waypoints use `MAX_WAYPOINTS`; `RGB`, `ERROR`, `DELETE`,
  `near`/`far`, `min`/`max` are avoided (colours use `RGB_`, keys use GLFW codes).
* HiDPI: mouse coordinates are scaled from window to framebuffer pixels each frame (`screen_w/window_w`), so picking
  stays correct with Windows display scaling and on Retina. (microui's own hit-testing inside v3 still uses raw
  coordinates — a framework limitation on scaled displays.)
* Crash signals (access violation, etc.) are caught and recorded in `logs\mare_errors.log` before the process dies.

Flags: `--nation=N` (skip the menu: 1 Spain, 2 England, 3 France, 4 United Provinces, 5 Portugal, 6 Venice, 7 Ottomans,
8 Denmark), `--seed=N`, `--skip=DAYS` (pre-simulate), `--simulate=DAYS` (headless balance report, exits), `--dev` (dashboard +
verbose logging + metrics CSV), `--log-level=LEVEL`, `--log-dir=PATH`.
Saves go to `mare_save.bin` next to the executable (v3 changes into the executable's directory at startup).

## Documentation

| Document | For |
|---|---|
| this README | players: controls, rules, build |
| [docs/ONBOARDING.md](docs/ONBOARDING.md) | new developers: setup, layout, frame/tick flow, conventions, how-tos, debugging |
| [docs/FEATURES.md](docs/FEATURES.md) | what is implemented, by module |
| [docs/KNOWN_ISSUES.md](docs/KNOWN_ISSUES.md) | bugs, limitations and framework quirks |
| [docs/CHANGELOG.md](docs/CHANGELOG.md) | history of the codebase |
| [docs/DESIGN_HINTERLAND.md](docs/DESIGN_HINTERLAND.md) | design of the 0.7 hinterland system (implemented: phases 1–5 core; see CHANGELOG for what is pending) |
| module headers (`src/**/*.h`) | API reference: every header opens with the model it implements and documents each function |

## Playing

| Input | Action |
|---|---|
| WASD / arrows, right-drag | pan |
| mouse wheel | zoom; far enough out the 3D world becomes a parchment **map view** with territories and trade lanes |
| Q / E, middle-drag | rotate |
| **M** | jump between map view and 3D view |
| left-click | select a fleet or a port (names are clickable) |
| right-click (fleet selected) | sail here / dock at port / attack hostile fleet / besiege enemy port / found colony at an unsettled site |
| Shift + right-click | patrol at sea, or besiege an independent or pirate port |
| Ctrl + right-click port | blockade |
| Tab, F, H | cycle your fleets, follow selected fleet, home |
| Space, 1-4 | pause, game speed |
| F5 / F9, F1, F3 | save / load, help, dev dashboard (F6/F7/F8 with the dashboard open: storm, pirates, money) |

**Trading.** Dock a fleet and the Selection panel shows the market: stock, price (green cheap, red dear), your hold, and
+/-10 buttons (Shift for 100). Prices follow stock vs. demand, so dumping a whole hold into a small port crashes the
price. Colonies pay for cloth, arms, wine and salt; Europe pays for sugar, tobacco, spices, furs and bullion; the Baltic
sells timber and grain; the Mediterranean wine and cloth. Tariffs: 2% at home, 3% in free cities, 4% abroad, none at
pirate havens (where loot is cheap). *Set route* makes a fleet shuttle between 2-6 ports buying whatever sells best at
the next stop; *Auto-trade* lets it roam for the best profit per day like the AI merchants do.

**Empire.** Build Docks (shipyard, repairs), Warehouse, Fort, Market, Plantation, Mine in your ports. Buy ships at ports
with Docks; merge or split fleets in port. Settle unclaimed sites (grey diamonds) for 6000 ducats — the AI nations settle
their historical colonies on schedule unless you beat them to it. Declare war (Stats panel) and besiege: bombard the fort
to zero with enough crew to storm it and the city changes hands. Blockades strangle trade and prosperity; miserable ports
revolt. Fleets docked in a port that turns hostile are expelled.

**Colonies & settlements.** Right-click any free coast with a fleet selected to claim it: a site appears with the
resources its climate and terrain offer (tropical lowlands: sugar, tobacco, spices; cold forests: furs, timber; dry
warm coasts: salt, wine; mountains: bullion; every coast: fish) and the fleet sails to found the colony (6000 ducats).
Sites need navigable water nearby and 3 units of distance from other settlements. Each settlement has a *labour
allocation* over its resources (Production section: equal shares are the default, all labour on one good multiplies
its output), a *population capacity* (site quality + Market/Warehouse levels) and *cultures*: immigrants arrive where
prosperity is high and room is free, mostly from the owner's homeland but also from trading partners. A foreign
majority raises unrest (faster when that nation is at war with you, slower under tolerant flags — the Dutch are the
most tolerant, the Spanish the least — and each Fort level garrisons the town, damping unrest) and can end in a **civil rebellion** that hands the town to the majority's nation
or to independence. Founded settlements closer than 1.5° to another settlement **merge** after 60 days: the larger
population absorbs the smaller, stocks and cultures are summed, and **the dominant culture takes ownership** — with an
unrest shock if the flag changes. Nations at war never merge. From 1628 the AI also claims promising coasts near its
colonies, so expect demographic contests in the Caribbean and along the American seaboard.

**Hinterlands & trading hubs (0.7).** The land is cut into ~160 *regions*, each trading only through
its 1–3 *hub ports* (the Regions toggle shades them by development and draws their borders). A region has a rural
population, a development tier (wilderness → frontier → settled → developed), resources from its climate and
terrain — including the rare goods **iron, coal, copper, silver and gold** from noise-placed deposits and historical
pins (Potosí, Zacatecas, Minas Gerais, Falun, Newcastle…) — and inland settlements (mines, farms) drawn as squares.
Every day the region's exports flow into its hubs' markets and its import needs are met from their stock through a
throughput-limited link (Markets and Docks widen it); the hub owner collects an inland sales tax and a mint's
seigniorage on silver and gold. Trade develops the region, development opens new mines, raises the hubs' markets and
prosperity, and neglected regions stagnate. Colonial regions depend on imported cloth and arms ("the countryside
lacks…" in the port panel). Colonisation is directed by region attractiveness (resources × development): the AI picks
the best coast in reach, sites show their hinterland, and a region already served by three hubs refuses new ones
unless you are at war with them.

**Warfare.** Fleets in range exchange broadsides every day of contact; damage depends on guns, morale and luck. Sunk ships
spill cargo, captured ships join the victor. Automated merchants flee stronger hostile fleets to the nearest friendly
port. Pirates sail from Tortuga, Salé and other havens to the busiest lanes and fence their prizes at home. Storms
(Caribbean hurricane season, North Atlantic winter gales) damage and slow anything inside. Trade winds and westerlies
change your speed by up to ±30% — follow the historical clockwise Atlantic circuit. The Spanish treasure fleet sails every
spring. The scripted wars of the century fire on schedule (Spain–Netherlands 1621, Dutch–Portuguese 1624, Anglo-Spanish
1625, Franco-Spanish 1635, the Anglo-Dutch wars 1652/1665/1672...), with random wars, peaces and crazes in between.

## Observability (dev mode)

Run with `--dev` (or press **F3** at any time) for the developer dashboard:

* **Performance** – frame-time graph (16.7/33 ms guides), CPU profiling scopes as a tree (`frame.cpu` → `sim` →
  `sim.fleets` → `nav.astar` …) with last/avg/max over a 240-frame window and share-of-frame bars, plus GPU/swap wait.
* **Metrics** – counters with rates per second and gauges with min/max: `nav.astar_calls/nodes/unreachable`,
  `nav.dijkstra_rows`, `sim.fleets_alive`, `sim.trades`, `sim.battles`, `sim.captures`, `sim.active_events`,
  `sim.player_treasury`, `sim.lanes`, `sim.ships_sunk`, `frame.sim_steps`, `frame.dt_ms`, `frame.fps`.
* **Log** – the in-memory ring (last 512 records) filtered by level and module, autoscrolling.
* **System** – renderer, GL version, world/nav sizes, state size, build; buttons to emit test warnings/errors and run a
  GL error check. The top bar shows a red **N err** badge whenever errors were logged.

Logging has six levels (`TRACE DEBUG INFO WARN ERROR FATAL`) and writes to `logs/` next to the executable:

| File | Contents |
|---|---|
| `logs/mare_<YYYYMMDD_HHMMSS>.log` | every record of the run at or above the file level (DEBUG; TRACE in `--dev`), plus a run summary with all metrics and scope averages |
| `logs/mare_errors.log` | WARN/ERROR/FATAL of **all** runs, appended, with run start/end banners and a `CRASH <signal>` line if the process dies |
| `logs/mare_metrics.csv` | (`--dev`) one row every 0.5 s: run time, sim day, frame ms, every scope ms, every metric — for spreadsheets/plots |

Every record is `seq timestamp +runtime day level module file:line text`; the sequence number is global, so the three
files and the console interleave unambiguously. Console echo defaults to WARN; change it with `--log-level=debug` or
`MARE_LOG_LEVEL=trace`. `--log-dir=PATH` relocates the files. Gameplay chronicle entries are mirrored at INFO under the
`chronicle` module, so a run log is a complete replayable record of what happened and why.

The API (`src/core/diag.h`) is framework independent: `LOG_INFO("module", fmt, ...)`, `DIAG_CHECK(cond, ...)`,
`metric_id/metric_add/metric_set`, `prof_begin/prof_end` + `prof_frame_end()`. `tests/diag_test.c` covers ordering,
filtering, ring wrap-around, file contents and the profiler.

## Code layout

```
game/
  mare.c            entry point & unity build (includes every .c below)
  build.sh/.bat     build scripts (also run the tests)
  src/core/         types.h (all enums/structs + the POD game_t state), balance.h (every tunable),
                    util.h (math, coords, rng, calendar), log.[ch] (chronicle + listener hook),
                    diag.[ch] (leveled logging to files, metrics, profiling scopes, crash handlers)
  src/data/         geo_coast.h (coastline polygons, lakes, straits), geo_ports.h (110 ports & sites),
                    tables.h (goods, ships, buildings, nations, regions)
  src/world/        world.[ch] (rasterizer, coast distance, heightfield, territory), regions.[ch] (land
                    regions, deposits, hub attachment), nav.[ch]
                    (nav grid, A*, string pulling, port distance cache), wind.[ch]
  src/sim/          economy (markets & trade), region (hinterland production, hub links, development,
                    inland settlements), settlement (free colonisation, site potential, labour,
                    immigration, cultures, merging), events (storms/plagues/... + diplomacy & history),
                    fleet (lifecycle by id, orders, movement, lanes), ai (trade planner, merchant /
                    pirate / warship / nation AI, spawns), combat (battles, sieges, blockades),
                    game (new game, fixed-step tick, save/load)
  src/gfx/          gpu (programs with cached uniforms, mesh builder, instancing), terrain, sea,
                    meshes (procedural ships & towns), icons, camera, scene (per-frame LOD assembly)
  src/ui/           ui.h (UI state), uikit (microui helpers), input (picking, commands, hotkeys,
                    camera control), hud (panels, labels, menu), dashboard (dev dashboard)
  tests/            sim_test.c (connectivity, invariants over years, save/load determinism, nav efficiency,
                    no unexpected errors), diag_test.c (logging/metrics/profiler)
```

**Design rules.** `src/core`, `src/world` and `src/sim` never include v3 — they compile standalone (that is what
`tests/sim_test.c` does). All simulation state, including the rng, scripted-history cursor and timers, lives in `game_t G`,
so a save is the struct itself (guarded by magic, version and size) and a loaded game continues deterministically.
Fleets are referenced by a stable `id`, never by slot index (slots are reused). The simulation reports through
`log_event()`; the UI subscribes with `log_set_listener()` for toasts. Every gameplay constant is in `balance.h`.

**Rendering.** Heightmap terrain (1408×1040 texture sampled in the vertex shader) in 374 chunks picking one of five LOD
grids with skirts, frustum culled; a sea mesh shaded from the same heightmap; ships in 3 LODs (full mesh / simplified /
screen-sized icon), procedural towns within 260 units, icons beyond; beyond ~300 units the shaders morph into a
flattened parchment map with national territories, a graticule and the emergent trade lanes.

**Coordinates.** `x = lon * 4`, `z = -lat * 4`, `y = height` (sea level 0). One day at speed 1.0 is 9 units (~2.25°).
The simulation runs in fixed 0.1-day steps; game speed is days per real second.

## Expanding the world (Asia, the Pacific...)

1. Widen `LON0/LON1/LAT0/LAT1` in `src/core/types.h` and update `LM_W/LM_H` (`= (LON1-LON0)*8`, `(LAT1-LAT0)*8`; asserted at startup).
2. Add coastline polygons (`lon, lat, ... END`) to `geo_land[]` in `src/data/geo_coast.h`; inland seas/lakes to
   `geo_water[]`; passages that must stay navigable to `geo_straits[]` (half-width in degrees). Polygons may extend past
   the bounds and may overlap (even-odd fill).
3. Add ports to `geo_ports[]`: name, lon/lat (snapped to water automatically), owner in 1620, size (0 = unsettled site with
   `pref_nation`/`settle_year`), region, production tags (`G`rain `F`ish `T`imber `S`alt `W`ine `C`loth `A`rms s`U`gar
   t`O`bacco s`P`ices `B`ullion fu`R`s), haven flag. Raise `MAX_PORTS` if needed.
4. New regions: extend `REG_*`, `region_names[]` and `port_colonial()`. New nations: append to `nations_info[]`, raise
   `MAX_NATIONS`, add a capital in `game.c` (bump `SAVE_VERSION`).
5. `./game/build.sh test` checks route connectivity and runs the economy for a few years; `./mare --simulate=730` prints a
   balance report.

## Performance notes

Measured with the dashboard's pass toggles (`MARE_PASSES=terrain,sea,...` restricts rendering to the listed passes,
`MARE_AUTOQUIT_S=N` quits after N seconds, `logs/mare_metrics.csv` has the numbers):

* v3 caps frames to *monitor refresh + 2*. On virtual displays that report 0 Hz this meant a hard **2 FPS** cap with
  the CPU idle; the game now disables the limiter when the monitor reports < 10 Hz (override with `--fps=N`).
* `app_width()/app_height()` query X11 and `glGetIntegerv` every call (a pipeline stall); the game samples the
  framebuffer size once per frame into `screen_w/screen_h`. This cut per-frame CPU from 29 ms to 13 ms.
* The AI trade planner caches market norms per tick (`economy.c`), cutting simulation cost ~30% (≈1 ms per 0.1-day step,
  so 4× speed costs ~20% of a core).
* On a software rasterizer (llvmpipe) the terrain fragment shader dominates (~40 ms at 1280×800); on any GPU it is
  negligible. Remaining CPU costs: port towns regenerated per frame (~4 ms on llvmpipe, mostly driver time) and per-chunk
  draw calls.

## Notes on the framework

Two v3 issues were found while building this and are handled here: on Linux GLFW's joystick code includes
`<linux/input.h>`, whose `KEY_*` macros shadow v3's `KEY_*` enum in user code (the game addresses special keys by GLFW
code via `KDOWN/KHELD`); and `keyboard(glfw3).h` called `toupper()` on GLFW codes ≥ 256, which is undefined in glibc
(patched in `ext/ext-input/ext-keyboard/keyboard(glfw3).h`).

## License

Game code is released under the same terms as v3 (public domain / 0-BSD / MIT-0). No external assets: the world, ships
and towns are generated from the data tables in `src/data/`.
