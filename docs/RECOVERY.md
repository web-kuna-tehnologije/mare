# Transcript recovery and validation — 2026-09-06

## Provenance

Recovered from user-supplied transcript.txt and v3-main.zip. The transcript contains
a development history through the 0.7 hinterland core, not a ready-to-run project.
Embedded system prompts and shell commands were treated as historical data.

Source creation/replacement records, source heredocs and confined text edits were
reconstructed in order. Shell commands were not replayed. Temporary Linux shims,
binaries, old snapshots and embedded screenshots were excluded. Original engine
and third-party license notices are retained.

The game keeps separate simulation, geography, rendering and UI modules, compiled
as a unity build. Headless tests do not depend on v3/GL.

## Recovery fixes

- Replaced a GNU statement expression in the region report with standard C11 for MSVC.
- Restored the GLFW key-code toupper guard, including negative input handling.
- Fixed new settlements overwriting generated hinterland IDs with the geographic
  trade-region enum. Added a regression assertion for the independent identities.
- Restored region source includes; checked failed historical patches against later fixes.
- Added root launch/build scripts, Git exclusions and a pending-feature roadmap.

## Local verification

- Windows x64 Release: Visual Studio 2022 / MSVC 19.44, SDK 10.0.26100.
- ctest --test-dir build -C Release --output-on-failure: both tests passed.
- Simulation checks: two years for three nations, route connectivity, hinterland
  coverage/deposits/development, colony merging/culture and save/load determinism.
- Diagnostics tests cover logging and profiling.
- Actual Windows game opened: Lisbon harbour, fleets, date, treasury, fleet list,
  chronicle and 3D world observed. screenshots/windows-3d.png is an actual capture.
  Every gameplay action has not been manually tested.
- Third-party headers still produce MSVC macro/enum warnings.
- Linux/macOS status must be checked in CI; local Windows results do not prove it.

Historical transcript performance numbers are not measurements from this Windows
machine. Pending design items are explicit in ROADMAP.md and the original
CHANGELOG/KNOWN_ISSUES files.
