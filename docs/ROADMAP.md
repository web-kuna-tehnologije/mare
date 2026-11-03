# Development roadmap

## Done: 0.22.25 — the oars, and why four rounds of fixing them made them worse (worktree `elm`)

The asset loader **re-fits every part**: `galley_oar` loads 0.26 units long, not the raw GLB's
2.583. 0.22.21 sized the sweep from the raw figure and drew every oar as a 0.43-unit sliver -
and the numeric probe added in the same round measured the raw model too, so it reported a
handsome oar that was never on screen. Four rounds of reports and "fixes" all argued from that
fiction. Restored to the pre-claim mount, with the probe rebuilt on the *loaded* bounds.

The lesson generalises, and issue20 hands us the next case: **22 asset LOD-extent warnings**
("LOD n extent differs from LOD 0 - wrong node?") on the masts, bowsprit and yards. That is
the same fault - a part not being what the code believes it is - and it is the next thing to
look at.

## Done: 0.22.24 — the oars put back, bridges stood, roads graded by trade (worktree `elm`)

The 0.22.23 rowing animation had broken the oars by moving the same value the rowlock mount
solves their placement from; reverted to the geometry that was verified numerically in
0.22.21. Then three rows off the list: **stone bridges** where roads cross rivers (16 in a
ring, of 218 crossings), roads **graded by where trade actually flows** - the old test read a
counter that is zeroed daily, so every road in the world was a track - and a **fruit-tree set**
for the orchards in place of forest species scaled down.

## Done: 0.22.23 — issue19: the wake survives the foam, masts fall, cordage is solved (worktree `elm`)

The sea's whitecaps had been *erasing* the wake - every source of white summed and clamped, so
full foam meant solid white and the trail went with it. Masts now go by the board as an
animation with their canvas, a battered hull wears shot scars and soot, oars row a real
drive-and-recovery stroke, fire breaks into separate licks instead of streaming, and villages
are the size of towns on the map.

And the **verlet cordage** finally landed, four rounds after it was first asked for: a
four-node chain per ship, position-Verlet with constraint relaxation, driven by her heel, the
whip of her roll and the wind across her deck. A chain *per rope* still waits on moving the rig
off its single per-class mesh - the one piece of plumbing left.

**Measured and dropped:** the broadphase and multithreaded physics jobs. The tactical world
costs 0.05 ms a frame with 40 bodies; there is nothing there to win.

## Done: 0.22.22 — issue18: smoke that climbs, ten real classes, crew and powder (worktree `elm`)

Smoke leaves a fire hard, is checked by the air, and then rises in a column - four times the
volumes, twice the rate, lives twice as long. The fleet is ten real classes in three roles
(four traders, three interceptors, three of the line), every authored hull in the tree now
sailed by one of them. And the two things the muster list had been honestly leaving blank are
simulated: **crew and ammunition** - balls kill men, a dry ship falls silent - while a **yard**
finally takes damage back off, so a campaign no longer only grinds down.

## Done: 0.22.21 — issue17: the delivered map finally read, oars to scale, fire that stands up (worktree `elm`)

The biggest find cost nothing to fix and explains the issue's first line: the game had never
been reading the user's extracted 4x terrain - the dataset probe scanned one directory level
and the zip extracts two. The oars' recurring fault fell to a numeric check (they were drawn at
hull scale - sweeps as long as the ship); flames stand up about the world's vertical with
sparks and three-tone smoke; fine terrain stops streaming at map zoom; a 60 fps limiter is in
Options; the authored tree LOD carries to the prop ring's edge; canvas burns away from the top
down; the wave foam lies along the swell at two-thirds strength.

Outstanding from it: one river-packing command in the user's extraction (182b), which a
read-only session cannot run into assets.

## Done: 0.22.20 — issue16: fire that burns, oars measured, towns with streets (worktree `elm`)

The fire is tongues of flame instead of glowing discs; the oar asset was finally *measured*
(the blade was at +x all along, on a -45-degree diagonal - three rounds of fixes had been
mounting it inboard); a felled mast is actually removed from the drawn ship; collisions and
running aground cost timber, and a ship can be stranded; the player can take in sail; the
battle has a muster list (enemy top, player bottom, condition bars per ship); villages match a
small town's footprint and towns stand ring streets inside towered walls.

## Done: 0.22.19 — issue15, and the battle grows teeth (worktree `elm`)

The fifteenth issues folder, then the physics and battle rows. The battle is no longer a
spectacle: the player gives standing orders (what to fire at, whether to board, run or hold),
the guns throw real shot with calibre, drag, curvature and a lay that follows the rolling
deck, ships catch fire and blow up, a wounded battery fires slower, a wreck settles by the
head - and, at last, **the result goes home**: tactical damage is written into the strategic
ships, so a battle costs a fleet something that outlives the scene. Relief down to 6; the map
ships heel to the wind instead of gliding; the authored rivers are chained and smoothed.

This closes the roadmap's long-standing **"the butcher's bill goes home"**, which has been the
highest-value item on the list since the damage model was written in 0.22.17.

## Done: 0.22.18 — the 4x world and its rivers (worktree `elm`)

The Mare Liberum 4x terrain build (2049 samples a chunk, ~700 m) is read, streamed and drawn:
the budget counts chunks instead of megabytes, the GPU chunk atlas picks its slot grid from the
dataset, and the geography fold reads a finer build at a stride so its cost no longer scales
with the data's resolution. The package's rivers are carved and navigable, by a class read off
the layer's own upstream lengths, and the roads bridge them.

This closes the roadmap's standing **"a genuinely finer DEM for the home seas"**, open since
0.20 and the highest-value terrain item on the list - though not in the form expected: the
answer came as a 4x resample of GEBCO for the whole Earth rather than EU-DEM and EMODnet for
the Adriatic. The caveat in DESIGN_TERRAIN.md 3d still stands at the new scale: GEBCO's own
grid is ~460 m at the equator, so 700 m output is the end of the source's detail. Anything
finer has to be procedural, or a genuinely different survey.

## Done: 0.21 — Vulkan draws, DX12 wakes up, the right GPU, an Options panel (worktree `oak`)

The discrete GPU is asked for (3.85x), Vulkan renders a SPIR-V pipeline offscreen and verifies
it, DX12 brings up a device and queue at feature level 12.2, the player has a graphics Options
panel that persists, the geography fold reads the export's float32 chunks on the job pool
(4.1x faster), and the last header-only modules are classes with one definition each.

## Next

- **Port a real pass to Vulkan.** Everything under the passes is proven; the next honest step
  is one pass end to end - compile the GLSL shader library to SPIR-V as a build step, build a
  pipeline from `rhi_pipeline_desc_t`, move its uniforms into a descriptor set, and render it
  offscreen next to the GL frame for comparison. Presenting comes last, because the swapchain
  has to replace the window's GL context rather than share it.
- ~~The butcher's bill goes home~~ — **done in 0.22.19** (todo 153). What remains under that
  heading is the campaign side of it: a yard that *repairs* the damage a fleet brings home, and
  a crew that can be killed and has to be replaced.
- **Per-rope verlet geometry** (todo 121b, the remainder): the rig off its single per-class
  mesh onto per-ship dynamic geometry, which also lets a felled mast take its own shrouds.
- ~~A genuinely finer DEM for the home seas~~ — **done in 0.22.18** by the 4x world build.
  What remains under that heading is procedural micro-detail on the near ground, not data.
- Finish T9: move callers onto the class APIs and inject `game_t`, which is what lets the
  C-ABI globals and their shims retire.
- A beach profile for the islands `raise_small_islands` lifts; they still read as plateaux.
- UI decision (RmlUi vs the classed-up kit); the mast far-LOD budgets; the bowsprit steeve.

## Done: 0.20 — the export read as chunks, a measured height atlas, Vulkan's memory path (worktree `oak`)

`assets/data/how_to.txt` is now implemented as written: the GEBCO export is read as 27x14
chunks of 513x513 float32 metres with their coverage masks (`world/terrain_chunks.h`), feeding the
clipmap rings ahead of the u16 cut. A GPU height atlas samples resident fine tiles at their
own resolution from the tessellation stage - and measures that, on the data shipped today,
there is nothing below the ring's resolution to add, so it stays off until a finer DEM lands.
Vulkan's memory, command, queue and fence layers are implemented and verified by a checked
GPU transfer.

(0.20's next items are folded into 0.21's list above.)

## Done: 0.19 — the authored rig, simulated flags, a shore that follows the Earth, a device behind the RHI (worktree `oak`)

Authored lantinas and split square yards stand on every station and swing with their canvas;
cordage is physical hemp through a geometry stage; flags are verlet cloth; canvas, water and
ground got their materials; the clipmap rings and the sea follow the authored shoreline inside a
band around the raster coast; `rhi::Device` sits behind the C ABI with the GL backend behind it,
a Vulkan bring-up device (instance/device/queue, no drawing yet) and a DX12 stub.

(0.19's next items are folded into 0.20's list above.)

## Done: 0.18 — C++20 end to end, the real Earth, a living rig (worktree `oak`)

The game and engine are C++20 classes behind stable C shims (docs/CPP_CONVERSION.md,
docs/REWRITE_PLAN.md); the terrain streams real GEBCO bathymetry with a fine Mediterranean
set and tessellates on GL 4.0; masts/yards/sails are authored parts whose canvas and spars
answer the wind as one body. Known polish items live in docs/TODO.md "Leftover tasks".

(0.18's next items - the Device abstraction, the Vulkan start, the coastline at close zoom -
landed in 0.19 above.)

## Done: world streaming 0.10 (game/docs/DESIGN_STREAMING.md)

The world is now a streamed hierarchy — continent -> region -> coastal tile -> tactical
chunk — under a memory budget with an LRU unloading policy and a disk tile cache; cities load
progressively (icon -> LOD4 -> ... -> LOD0); the per-entity tick phases are split into
parallel compute + ordered commit on the job system (adaptively dispatched: the pool wins only
past ~2k entities, measured); the sea's wave height follows the zoom. All eight tasks of the
design landed with a headless `stream_test`, a dashboard Streaming tab and the tile overlay.

Follow-ups:
1. [x] **Normals from the detail window** (0.10.1) — the terrain shader differentiates the
   composed delta and lights the relief it displaces.
2. [x] **A second, coarser clipmap ring** (0.10.1) — near 80 units for tactical detail, far
   320 for coastal, each sized by Nyquist against its level's detail frequency, the near one
   cross-fading into the far.
3. [ ] Tile-local content: streamed vegetation/rock scatter per tactical chunk through the
   same residency machinery.

## Done: the Generation I fleet 0.11 (game/docs/DESIGN_SHIPS.md)

Ships are assembled from authored hulls, rigs and oar banks imported from Meshy
(`tools/meshy_import.py`), mapped to game ship types by nation, with the procedural hulls as
the fallback. The map can be generated a chunk at a time: `mare --map-chunk=` writes the
reference image and placement brief for a streamed tile, and the importer turns the result into
a `tile_l<level>_<x>_<y>` asset.

Follow-ups:
1. [ ] Generate the Great Carrack (50-60 m) so the Ship of the Line stops standing in as a
   stretched war carrack, and sails for the bare rigs.
2. [ ] Wire the authored fleet into the staged battle/city views (`gfx/staged_scene.cc`), which
   still stage the procedural hulls.
3. [x] **Free an imported LOD 0's CPU copy** (0.12.0) — `asset_lod0_trim` hands back any level
   that has sat un-uploaded for 45 s and reloads it if something later picks it.
4. [x] **Oar animation** (0.12.0) — the bank swings together at a rate and reach set by how
   hard the ship is being pulled.
5. [ ] Double WORLD_K to 8 so the world is not vertically exaggerated tenfold (DESIGN_TERRAIN.md section 4): it moves every world coordinate, so it wants its own pass.
6. [ ] Sail and flag textures, and cloth that responds to the wind rather than a fixed flutter
   (the per-vertex weight channel and the instance cloth flag are already in place).
6. [ ] Rigging is a fixed pattern per class; running rigging (braces, halyards) and yards that
   brace round with the wind would make the rig read as worked rather than rendered.
4. [ ] Raise the world/port/fleet caps toward the full-map scale the adaptive parallel tick is
   gated for, and re-measure `MARE_PARSIM=2` against the 2 ms dispatch cost.

## Done: renderer modernisation (game/docs/DESIGN_RENDERER.md)

All ten steps are complete: RHI abstraction, HDR, linear/sRGB, PBR, directional lighting,
cascaded shadows, normal maps, frustum culling, instancing and CPU/GPU/VRAM/RAM profiling.
Possible follow-ups: a Vulkan or DX12 backend behind the RHI, terrain that casts shadows from a
dedicated caster mesh, ship wakes, and a day/night cycle driving the existing sun.

The recovered baseline implements the transcript's 0.7 core. These remaining
ideas are planned features, not claims of completed work.

## Next: hinterlands 0.8

1. Standalone region panel: production, shortages, hub capacity and development.
2. Player rural labour allocation, separate from existing port production controls.
3. Chartered inland settlements with costs, ownership and gameplay feedback.
4. Interior settlements switching hubs when ports merge, fall or change hands.
5. Per-good shortage targeting for AI traders, with reproducible balance scenarios.
6. River links and Market/Docks capacity effects validated by throughput tests.
7. Copper hull upgrades and iron shipyard discounts with explicit resource costs.

## Next: visual rendering 0.9 (game/docs/DESIGN_VISUAL.md)

Nine steps, in order: occlusion culling, anisotropic filtering, static shadow caching, the
wind-driven sea (replacing the fixed wave set and fixing the camera-speed artefact),
tessellation and normal maps, foam and breaking waves, calm weather, clouds and the sun,
ambient occlusion. Each step lands with a toggle and a before/after capture.

## Presentation and long-term foundations

- Smoother region borders and more historical pins.
- Fleet label collision avoidance and framework HiDPI panel input.
- Ship/city art, weather, onboarding and repeatable performance scenes.
- Portable versioned save serialization instead of a compiler-dependent struct dump.
- Balance AI treasury inflation and coordinated naval warfare.
- Expand Africa and Asia with data-driven geography, ports and resource regions.

Before each feature: update its design, implement a playable slice, add meaningful
simulation tests, visually check affected UI, then update FEATURES and CHANGELOG.

## In progress: engine scale 0.8 - job system and world LOD

Two designs drive this work: [DESIGN_JOBS.md](../game/docs/DESIGN_JOBS.md) (multithreading) and
[DESIGN_LOD.md](../game/docs/DESIGN_LOD.md) (LOD management). The goal is no stall during tick
processing, world traversal, zooming or LOD streaming, at every scale from a town square to the
whole Atlantic.

The job system comes first because the LOD streamer is built on it. Each step lands fully
working on all three platforms, with tests and a measurement, before the next starts.

1. [x] **C++ toolchain + job system core** - `core/jobs.h` (C ABI) and `core/jobs.cpp` (C++17,
       the project's first C++ translation unit): worker pool, the WORKER/IO/DECODE/MAIN lanes,
       join counters, `jobs_parallel_for`, the main-thread drain, statistics, `MARE_JOBS`
       synchronous mode. `tests/jobs_test.cpp`; build.sh / build.bat / CMake / CI updated.
2. [x] **Asset loading on the pools** - split `asset_load` into disk read (IO), glTF parse,
       texture decode and mesh processing (DECODE) and GPU upload (MAIN, inside a per-frame
       budget); non-blocking `asset_request` with an explicit state machine; parallel startup
       preload. Removes the ~800 ms per-asset startup stall.
3. [x] **Screen-space LOD selection** - `gfx/lod.h/.c`: projected-pixel metric, authored
       distances converted to pixel thresholds at load, LOD bias, per-instance state with
       hysteresis. Ships, towns and authored assets all select through it.
4. [x] **Stable transitions** - per-instance fade in `inst_t`, dithered cross-fade in the mesh,
       depth and smoke shaders, mesh-to-icon dissolve, a `lod.pops` metric that must stay at 0.
5. [x] **View tiers and the LOD director** - city / battle / regional / strategic / continental
       tiers with per-tier policy and budgets, demotion of the smallest instances when over
       budget (generalises today's `BATTLE_DETAIL_MAX` ship promotion).
6. [x] **Icons and clustering for distant objects** - icon alpha coupled to the mesh fade, size
       and shape by importance, screen-space clustering at continental zoom.
7. [x] **LOD streaming and residency** - wanted-set prediction from camera velocity, request and
       eviction through the job system, VRAM and upload budgets, coarser-LOD fallback so a
       stream-in never leaves a hole, shared cloud cover (`gfx/clouds.c`) for zooms that outrun
       the streamer.
8. [x] **Simulation work on the job system** - the port-distance rows (the only simulation work
       big enough to show in a frame: ~15 ms each, 111 of them) are precomputed in parallel at
       game start instead of landing lazily inside ticks. `sim_test` links the real pool, so its
       save/load determinism check now covers that work. The per-entity phases (economy,
       movement, regions) measure at 1-2 us each and are left sequential on purpose: see
       DESIGN_JOBS.md, "What is not worth parallelising".
9. [x] **Async save / load** - `game_save_async` snapshots the state on the main thread (a
       ~1 MB copy, 0.1-0.3 ms) and writes it on the IO lane to `<name>.tmp`, renamed over the
       old save when complete; `game_load_async` reads and validates on the IO lane and the
       state is swapped in by a per-frame poll. Autosave every 180 s of unpaused play
       (`mare_autosave.bin`, loadable from the menu). Measured: the worst frame while an
       autosave was in flight is indistinguishable from the run's normal frames; `sim_test`
       checks the asynchronous files are byte-identical to the blocking save's and that a
       state applied by the poll continues bit-identically.
10. [x] **Observability and evidence** - dashboard **Jobs** tab (lane occupancy, queue depths
        with history, jobs/s, inline runs, blocked-wait time, pipeline stages in flight,
        save/load state) and **LOD** tab (tier and budgets, director scale, instance counters,
        GPU residency per asset and LOD, streamed textures); `jobs.busy_pct`, `jobs.wait_ms`,
        `jobs.io_queued`, `save.snapshot_ms`, `io.save_ms`, `io.load_ms` metrics; `long frame`
        DEBUG records attributing any frame over 25 ms; `tools/metrics_summary.py`; the
        before/after table in DESIGN_JOBS.md for the fully zoomed-out map, the 50-ship battle
        and the scripted full-range zoom; FEATURES, CHANGELOG, KNOWN_ISSUES, ONBOARDING, both
        READMEs updated.

Draw submission stays single-threaded (the GL context belongs to the render thread); batching
and instance-buffer reuse already keep it cheap.

Landed alongside steps 5-8 (0.8.3): 3D Gerstner waves the ships ride, the authored sea in the
staged scenes, a day/night cycle driving the shadows, and a bigger battle framing.

Landed alongside steps 2-4 (0.8.2): the authored sea tiles (`game/assets/sea`, streamed through
the same lanes), a Blender LOD exporter + descriptor generator for the Meshy building set, and
the first use of those buildings in towns and the staged city. The next step that matters most
on integrated GPUs is 5: the city view with ~150 authored houses is triangle-bound until the
director budgets LOD 0.
