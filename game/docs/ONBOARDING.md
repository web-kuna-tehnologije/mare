# Onboarding guide

Welcome aboard. This document gets you from a fresh checkout to productive changes: how to build
and run, how the code is organised, how a frame and a simulation step flow through the modules,
the conventions you must respect, and how to debug. Read it alongside `README.md` (player-facing)
and `FEATURES.md` / `KNOWN_ISSUES.md`.

## 1. Setup

| Platform | Prerequisites | Build | Tests |
|---|---|---|---|
| Linux | gcc, `libx11-dev libgl1-mesa-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libasound2-dev` | `./game/build.sh` → `./mare` | `./game/build.sh test` |
| Windows | Visual Studio 2019+ (C++ workload) | `game\build.bat` → `mare.exe` (`debug` for symbols) | `game\build.bat test` |
| macOS | Xcode command line tools | `./game/build.sh` | `./game/build.sh test` |
| Any IDE | CMake 3.16+ | `cmake -S game -B build && cmake --build build` | `ctest --test-dir build` |

The game is a **single translation unit**: `game/mare.c` includes `v3.h` (the framework) and then
every `src/**/*.c`. There is no separate link step and no library to install. The headless tests
include only `src/core`, `src/world`, `src/sim` and need nothing but libc/libm.

Useful flags: `--nation=4 --seed=7` (skip the menu), `--skip=365` (pre-simulate), `--dev`
(dashboard, verbose log, metrics CSV), `--simulate=730` (headless balance report), `--fps=N`,
`--log-level=debug`, `--log-dir=PATH`. Environment: `MARE_LOG_LEVEL`, `MARE_PASSES=terrain,sea`
(restrict render passes for profiling), `MARE_AUTOQUIT_S=30`.

## 2. Directory map

```
game/
  mare.c                  entry point: arguments, init, the frame loop (read this first)
  build.sh / build.bat    canonical builds;  CMakeLists.txt for IDEs;  docs/  tests/
  src/core/               framework-free foundations
    types.h               every enum/struct and the POD state `game_t G`  (ALL simulation state lives here)
    balance.h             every gameplay tunable (no magic numbers elsewhere)
    util.h                math, coordinate conversions, rng (xorshift32), calendar, colours
    log.[ch]              the player-facing chronicle (+ listener hook for toasts)
    diag.[ch]             technical logging (levels, files, ring), metrics, profiler, crash handlers
  src/data/               static data: geo_coast.h (polygons), geo_ports.h (ports/sites), tables.h (goods, ships, buildings, nations)
  src/world/              world.c (raster, heightfield, territory)  regions.c (land regions, deposits)  nav.c (water grid, A*, port distances)  wind.c
  src/sim/                economy, region (hinterlands/hubs), settlement, events (incl. diplomacy/history), fleet, ai, combat, game (new/tick/save/load)
  src/gfx/                gpu (programs/meshes/instancing), terrain, sea, meshes (procedural ships/towns), icons, camera, scene
  src/ui/                 ui.h (UI state + kit API), uikit, input (picking/commands/hotkeys/camera), hud (panels), dashboard
  tests/                  sim_test.c (connectivity, invariants, colonisation, save/load determinism)  diag_test.c
```

**Dependency rule:** `core` ← `world` ← `sim` never include v3 or GL. `gfx` and `ui` may use
everything. If you find yourself wanting the simulation to call the UI, emit a `log_event()` or a
metric instead and let the UI react.

## 3. How a frame flows (`mare.c`)

1. `app_swap()` (v3) presents the previous frame and polls events. We sample `screen_w/h` and
   `window_w/h` once (v3's `app_width()` is expensive, see KNOWN_ISSUES).
2. `ui_overlay_begin()` opens the label layer. `ui_mouse_over_panel()` tells input whether the
   mouse is on a panel.
3. `input_frame()` → picking (`pick()`), left/right click commands, hotkeys, camera input;
   `cam_update()` smooths and rebuilds matrices.
4. Simulation: `sim_acc += dt * G.speed`; `game_tick(SIM_STEP)` runs while ≥ 0.1 day is owed
   (max 40 steps/frame, then days are dropped with a warning).
5. Rendering: `RF` parameters + `render_light_update()` → `scene_build_ports/ships` (fills the
   instance lists) → `shadow_render()` (three sun cascades into the depth atlas) → `hdr_begin`
   (RGBA16F target, cleared in linear) → `terrain_draw` → `meshes_bind` + `scene_submit_ports/ships`
   → `sea_draw` (blended) → `scene_draw_overlays` + `dd_flush` (v3 debug draw) → icons →
   `hdr_resolve` (tonemap + sRGB encode to the backbuffer) → `hud_labels`.
   Geometry is built once and submitted twice (shadow cascades, then the main pass).
6. `ui_overlay_end()`, then panels (`hud_panels`, `dashboard_panel`, or `hud_menu`). The UI draws
   on the backbuffer, after the tonemap.
7. `prof_frame_end()` rolls profiler statistics and samples metrics.

## 4. How a simulation step flows (`sim/game.c: game_tick`)

```
G.day += dt
economy_tick      every port: produce, consume (price-elastic), surplus decay, prosperity, unrest,
                  growth, fort regen, blockade expiry, taxes, prices  (+ weekly price history)
settlements_tick  immigration, assimilation, culture unrest, secession; merging once per day
regions_tick      rural production -> export pools, hub links (exports in, imports out, tax), development,
                  prosperity, inland settlements, hub shares (daily parts once per day)
events_tick       scripted history, storms move/expire, random events, war timers & peace
fleet_tick (all)  morale, upkeep, ai_think when think_timer expires, fleet_move (wind, storms,
                  arrival handling), siege_tick
combat_tick       pairwise hostile fleets: flee or broadsides; sinking/capture
ai_nations_tick   per-nation purchases, investments, colonisation, pirate spawns, treasure fleet
lanes_decay, daily stat rollover, territory recompute if dirty
```

Everything is deterministic for a seed: the rng is `G.rng`, time is `G.day`, and nothing the
simulation reads lives outside `G` except the static world. `sim_test.c` asserts that a saved and
reloaded game continues bit-identically — keep it that way (see rules below).

## 5. Conventions (please follow)

* **State**: anything the simulation needs to resume goes in `game_t` (bump `SAVE_VERSION` when
  the layout changes). Caches outside `G` must be rebuildable from `G` (`economy_refresh_caches`,
  `nav_ports_reset`) and must not change results (the nav distance rows are computed
  synchronously for exactly this reason).
* **References**: fleets by `id` (`fleet_by_id`), ports by index (ports are never removed; a
  merged settlement is flagged `merged_into` — always skip `port_is_gone()` ports).
* **Randomness**: only `rnd_*(&G.rng, ...)` in simulation code; `rnd_chance(&G.rng, p_per_day, dt)`
  for per-day probabilities. Rendering may use its own seeds.
* **Tunables** go in `core/balance.h` with a comment. Prices, rates and thresholds never inline.
* **Logging**: gameplay-visible events → `log_event[_at]()` (chronicle). Technical events →
  `LOG_INFO/WARN/ERROR("module", ...)`; errors are for things that should never happen.
  Anything that costs time → wrap in `prof_begin/prof_end`; anything countable → a metric.
* **Unity build**: every file-scope `static` shares one namespace. Prefix module statics
  (`terrain_prg`, `sea_vao`, ...). Header guards are `MARE_<NAME>_H`.
* **Portability**: C11 only — no VLAs, no statement expressions, no `__attribute__`, no POSIX
  in `core/world/sim` outside `diag.c`'s guarded block. Avoid Windows macro names (`MAX_PATH`,
  `ERROR`, `DELETE`, `near/far`, `min/max`). Never put a `game_t` on the stack (~1 MB).
* **Keys**: use `KDOWN(GLFW_KEY_x)` / `KHELD(...)`, not v3's `KEY_*` (shadowed on Linux).
* **Sizes**: use `screen_w/screen_h` (framebuffer) for projection and `mouse_px()/mouse_py()`
  for the cursor, never `app_width()` in per-object loops.

## 6. How to...

* **Add a good**: `G_*` enum in `types.h`, row in `goods[]` (tables.h), a tag letter in
  `geo_tag_good()`, demand modifiers in `port_consumption()`, potential rule in `site_potential()`.
* **Add a nation**: `NATION_*` enum + `nations_info[]` (colour, AI targets, tolerance, colony
  prefix), a capital in `game.c`, ports in `geo_ports.h`, scripted wars in `events.c`; bump
  `MAX_NATIONS`/`SAVE_VERSION`.
* **Add a region or continent**: widen `LON0/LON1/LAT0/LAT1` and `LM_W/LM_H` (asserted at start),
  add polygons to `geo_coast.h` (clipped, even-odd), straits to `geo_straits[]`, ports to
  `geo_ports[]`, `REG_*` + `region_names[]` + `port_colonial()`; run `./game/build.sh test` (route
  connectivity is checked).
* **Add an event**: `EV_*`, a `P_*` rate in `balance.h`, a block in `random_events()`, a colour in
  `hud.c: log_color()`.
* **Add a panel**: a `static void panel_x(void)` in `hud.c` using `mu_get_container` + `ROW`/`ui_btn`;
  call it from `hud_panels()`. Give repeated widgets `mu_push_id`.
* **Add a render pass**: a `.c` in `src/gfx` with a `program_t`, cached uniform names, a
  `*_init` called from `render_init()` (icons.c) and a draw call in `mare.c` wrapped in a
  `prof_begin/end` and a `U.pass_*` toggle.
* **Add a metric / scope**: `static int m = -1; if (m < 0) m = metric_id("mod.name");` then
  `metric_add/metric_set`; scopes likewise with `prof_id`. They appear in the dashboard and CSV.

## 7. Debugging workflow

1. Reproduce headlessly if you can: `./mare --simulate=DAYS --seed=N --nation=X --log-level=debug`
   prints the chronicle and a nation table; `logs/mare_<stamp>.log` has every DEBUG record.
2. In the window, **F3** opens the dashboard: Performance (frame graph, scope tree), Metrics,
   Log (level/module filters), System (GL info, test buttons, pass toggles). The top bar shows a
   red `N err` badge when anything was logged at ERROR; `logs/mare_errors.log` accumulates WARN+
   across runs with run banners and crash lines.
3. `MARE_PASSES=...` + `MARE_AUTOQUIT_S=25 ./mare --dev` and `logs/mare_metrics.csv` give
   reproducible per-pass timings (see README "Performance notes" for the method).
4. Debug keys (dashboard open): F6 storm at the camera, F7 pirates, F8 +100k ducats.
5. Tests: `./game/build.sh test [years]`. `VERBOSE=1 ./sim_test` prints fleets found on land.
   Add a scenario to `sim_test.c` whenever you fix a simulation bug.

## 8. Reading order for newcomers

`types.h` → `balance.h` → `game.c` → `economy.c` → `fleet.c` → `ai.c` → `mare.c` → `scene.c` →
`hud.c`. Each header starts with a paragraph explaining the model it implements.
