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

## Observability
- **L** `mare_metrics.csv` re-emits a header (prefixed `# columns changed`) when new scopes or
  metrics register after the first row; readers must tolerate comment lines (pandas: `comment='#'`).
- **L** Frame-time guides assume 60/30 Hz; the dashboard scale button cycles 16.7/33/66/133 ms.
- **L** Crash handlers write to the log from a signal handler (not async-signal-safe by the letter
  of the standard; acceptable for a dying process).

## Platform / framework
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
- Save files are the raw `game_t` (≈950 KB) guarded by magic/version/size; they are not portable
  across compilers with different struct padding (x64 MSVC/GCC/Clang agree in practice).

## Performance envelope
- Simulation ≈ 1 ms per 0.1-day step (≈110 ports, ≈130 fleets); speed 4 (20 days/s) uses ~20% of a
  core. Above ~40 steps per frame days are dropped with a logged warning.
- First-time Dijkstra rows cost ~22 ms each (one per port, lazily); expect small hitches in the
  first minutes of a new game and after founding a colony (one row).
