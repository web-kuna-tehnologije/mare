# Design: job system (multithreading)

Status: **all steps done** - `core/jobs.h` + `core/jobs.cpp` and `tests/jobs_test.cpp`
exist, the pool starts with the game, the render thread drains the MAIN lane every frame, the
asset runtime (`assets/asset.c`) and the streamed textures (`gfx/textures.c`) load through the
lanes exactly as the pipeline below describes, the port-distance rows are precomputed on the
WORKER lane, and save/load runs on the IO lane (`sim/game.c`, "Save and load" below). The
dashboard Jobs tab and the before/after table are at the end.

The game is single-threaded: one thread parses GLBs at startup (~800 ms for one asset), runs
every simulation tick, builds every instance list and submits every draw call. That is why a
zoom across the map stutters, why the first frame after a new asset appears hitches, and why the
40-step tick clamp exists at all. This document describes the job system that removes those
stalls, and the rules that keep the simulation deterministic while it does.

## Why a C++ translation unit

The engine is C11 and stays C11. A thread pool, however, needs threads, mutexes, condition
variables, atomics and thread-local storage on Windows, Linux and macOS at once. C11
`<threads.h>` is not available in MSVC, so in C this is ~400 lines of `#ifdef _WIN32` /
`pthread` wrapper that we would own and debug forever. C++17 `<thread>`, `<mutex>`,
`<condition_variable>` and `<atomic>` are present in all three toolchains the project already
requires (MSVC 2019+, gcc, Apple clang) and cost nothing at runtime.

So the job system - and only the job system - is C++:

* `src/core/jobs.h` is a **C header with a C ABI** (`extern "C"` when compiled as C++). Game code
  includes it exactly like any other header and never sees a C++ type.
* `src/core/jobs.cpp` is the single C++17 translation unit.
* The build gains a second TU and links with the C++ driver. `mare.c` stays the unity build of
  all the C sources. `build.sh` compiles both and links with `$CXX`; `build.bat` compiles
  `jobs.cpp` on its own and hands the object to the `mare.c` command line, because `cl` refuses
  `/std:c11` and `/std:c++17` together (D8016); CMake compiles per file and needs neither
  workaround.

This keeps the C conventions in ONBOARDING intact (no C++ in `core`/`world`/`sim`/`gfx` headers,
no exceptions crossing the boundary, no STL in the game's structs) while giving the threading
code a portable standard library. Later C++ islands - if any - follow the same rule: C ABI at
the boundary, one TU behind it.

## Lanes, not pools

Separate OS thread pools per task kind oversubscribe the machine: eight decode threads and eight
compute threads on an eight-core box means everything is 2x slower under load. Instead there is
**one worker pool** of `max(1, cores - 1)` threads serving several *lanes* (queues), plus
dedicated threads for work that blocks, plus a queue that only the render thread drains.

| Lane | Runs on | For | Why |
|---|---|---|---|
| `JOB_LANE_WORKER` | worker pool, **high** priority | tick phases, scene build, culling, LOD selection, streaming decisions | frame-critical: a `parallel_for` must not queue behind a JPEG |
| `JOB_LANE_DECODE` | worker pool, **low** priority | glTF parsing, texture decompression, mesh processing (normalise, pivot, material rules, texture bake) | long, latency-tolerant, must never block a frame |
| `JOB_LANE_IO` | 2 dedicated I/O threads | reading `.glb`/`.json`/save files, writing saves and logs | blocking syscalls; burning a pool slot on a disk wait can starve a `jobs_wait` |
| `JOB_LANE_MAIN` | render thread, drained once per frame with a time budget | GPU upload (`mesh_upload_indexed`, texture upload), anything touching GL or `RF` | the GL context belongs to one thread, and uploads must be rate-limited to avoid a 30 ms frame |

A worker takes from the high queue until it is empty, then from the low queue, then steals, then
sleeps on a condition variable. That ordering is the whole scheduling policy.

The asset pipeline is the motivating case and uses four of them in sequence:

```
asset_request("church")
  IO      read church_LOD0.glb from disk          (blocking read, 2 threads)
  DECODE  parse glTF  -> gltf_model_t             (CPU heavy)
  DECODE  decode base-colour PNG/JPEG (stb_image) (CPU heavy, parallel per image)
  DECODE  mesh processing: forward rotation, pivot, scale, material rules, texture bake per LOD
  MAIN    mesh_upload_indexed + asset_free_cpu    (render thread, inside the frame's upload budget)
```

As built: `asset_request()` allocates a per-load context, the IO stage reads every distinct
file, one DECODE job parses the models, runs the texture decodes as a `jobs_parallel_for`
(three 1k-2k JPEGs per Meshy export in flight at once) and builds the LODs into a *private copy*
of the asset, and a MAIN job commits the copy - so `asset_t.state` only ever changes on the
render thread and no lock is needed. The commit then submits one MAIN upload job per LOD, which
the frame's 2 ms budget spreads out. `asset_load()` (tests, tools) is the same chain with the
caller helping the pool (`jobs_help`) until the state flips. Measured: six assets, 322 ms each
*concurrently*, all resident 0.1 s after the window appeared, where one church alone used to
hold the start-up for 795 ms. The log path in `core/diag.c` took a spinlock for this, since
workers log.

## API

```c
typedef enum job_lane_t { JOB_LANE_WORKER, JOB_LANE_DECODE, JOB_LANE_IO, JOB_LANE_MAIN, JOB_LANE_COUNT } job_lane_t;
typedef unsigned job_counter_t;          // 0 = no counter
typedef void (*job_fn)(void *arg);
typedef void (*job_range_fn)(void *arg, int begin, int end, int slot);

int  jobs_init(int nworkers);            // <0 auto (cores-1, MARE_JOBS honoured), 0 synchronous. Returns workers started
void jobs_shutdown(void);                // drains, joins, frees
int  jobs_worker_count(void);
int  jobs_slot(void);                    // stable per-thread index for scratch/reduction buffers (main thread = 0)
int  jobs_slot_count(void);

job_counter_t jobs_counter(void);                                  // join handle; release with jobs_wait/jobs_release
void jobs_submit(job_lane_t, job_fn, void *arg, job_counter_t);    // counter 0 = fire and forget
void jobs_wait(job_counter_t);                                     // helps run jobs while waiting, then releases
int  jobs_done(job_counter_t);                                     // non-blocking check (does not release)
void jobs_release(job_counter_t);
void jobs_parallel_for(job_lane_t, job_range_fn, void *arg, int count, int grain);   // split, run, join (helping)
int  jobs_main_drain(double budget_ms);                            // render thread, once per frame; returns jobs run
void jobs_stats(job_stats_t *out);                                 // per-lane submitted/executed/queued/busy
```

Design decisions worth stating:

* **Waiting helps.** `jobs_wait` and `jobs_parallel_for` run queued jobs on the calling thread
  while they wait. A job may therefore wait on jobs it spawned without deadlocking, and the main
  thread contributes instead of idling.
* **No allocation on submit.** Each queue is a fixed-capacity ring (4096 entries) of
  `{fn, arg, counter}` PODs. Submitting to a full queue runs the job inline - back-pressure
  instead of a failure path nobody tests. Job *arguments* are owned by the caller; the asset
  pipeline keeps its stage state in the `asset_t` it belongs to.
* **Counters are handles**, not pointers: a slab of 1024 atomic counters plus a generation, so a
  stale handle is detected rather than corrupting someone else's join.
* **Synchronous mode.** `MARE_JOBS=0` or `jobs_init(0)` makes every submit run inline on the
  calling thread, including `JOB_LANE_MAIN`. It is also what the API does *before* `jobs_init`
  and *after* `jobs_shutdown`, so no call site needs an "are we initialised" check. It is the
  first thing to try when a bug smells like a race. `MARE_JOBS=N` pins the worker count.
* **Shutdown completes what is queued.** Pool and I/O work still in the queues is run; queued
  MAIN jobs are dropped, because by then the GL context may be gone - but their counters are
  completed either way, so nothing can hang on a join.
* **No exceptions, no RTTI** cross the boundary; `jobs.cpp` is compiled with them off where the
  toolchain allows it.

## Determinism rules

`sim_test` asserts that a saved and reloaded game continues bit-identically, and that must keep
holding with N workers. Therefore any simulation phase that runs on the pool must obey:

1. **Write only to per-entity storage** indexed by the entity (`G.fleets[i]`, `G.ports[i]`), never
   to shared accumulators, and never resize an array in `G`.
2. **No `rnd_*(&G.rng)`.** A parallel phase draws from a per-entity stream derived from the
   entity id and the day (`rnd_stream(G.seed, id, day)`), so the draw does not depend on the
   order the entities happened to be visited in.
3. **Reductions go through per-chunk partials**, summed afterwards in chunk order. This is why
   `jobs_parallel_for` derives its chunk boundaries from `(count, grain)` alone: the set of
   additions, and the order they happen in, is then identical whether one thread or sixteen ran
   the chunks. Per-*worker* partials would not be - the subsets would differ with the thread
   count and so would the rounding.
4. **Phases are explicit.** Each tick is a sequence of phases with a `jobs_wait` between them;
   nothing reads a value another phase is writing in the same phase. Read-only cross-entity
   access (fleet A reading port B's price) is allowed only in phases where nobody writes it.

Phases that cannot meet these rules (settlement merging, colonisation, anything that creates or
destroys entities) stay sequential. The speedup comes from the per-entity bulk - economy,
movement, regions - not from the bookkeeping.

`jobs_test` already asserts the chunk invariant: the same reduction run synchronously, with one
worker and with eight is bit-identical. When the tick moves onto the pool (step 8), `sim_test`
will additionally run the same seed with 1 and with N workers and compare the save blobs byte
for byte.

## Testing

`tests/jobs_test.cpp` is headless and covers: synchronous mode, every lane executing, counters
(including stale handles and an exhausted slab), `parallel_for` coverage for ragged and
oversized grains, chunk determinism across worker counts, a `parallel_for` nested inside a job
(the case that deadlocks a pool without helping waits), MAIN jobs running only on the render
thread, the drain budget stopping early and finishing later, back-pressure when a lane is full
(3x the queue capacity submitted at once), and shutdown completing queued work.

## What is not worth parallelising

The plan assumed the fixed 0.1-day ticks would want splitting across the pool. Measuring them
said otherwise: at the current world size `sim.economy` averages 1 us per frame, `sim.fleets`
2 us, and the rest less. Handing those to a thread pool would cost more in dispatch than they
take to run, and would put the save/load determinism guarantee at risk for nothing.

The simulation work that *was* worth moving is the port-distance cache. Each row is a Dijkstra
over the whole 704x520 water grid - about 15 ms - and with 111 ports those rows used to be
computed lazily, one inside whatever tick first asked for it, which is exactly the "stall during
tick processing" this work exists to remove. They are now computed together at game start
(`nav_ports_precompute`), split into 8 chunks on `JOB_LANE_WORKER`: **111 rows in 301 ms**
instead of ~1.7 s serial, and no Dijkstra at all during play (`nav.dijkstra` peaks at 0 ms
in-frame, where it used to spike to 45 ms).

Two things make that safe. Each chunk allocates its own search scratch, so the chunk count is
also the concurrency cap and no lock is needed; and a row is a pure function of the static water
grid and the port positions, so computing them eagerly and in parallel produces exactly the
values the lazy path produced. `sim_test` links the real job system and runs with the pool
active, so its save/load bit-identity check is now also the determinism check for this.

## Save and load (step 9)

A save is two things: a consistent copy of `game_t`, and a file write. Only the first has to
happen on the main thread, and it is a 1.1 MB `memcpy` - 0.1 to 0.3 ms measured, taken between
the simulation step and the scene build, so it is never mid-tick. `game_save_async(path)`
takes that copy into a heap buffer and submits one IO-lane job that writes `<path>.tmp`,
closes it, removes the old file and renames the new one over it. The previous save is therefore
intact until a complete new one exists (remove + rename rather than a bare rename because MSVC's
`rename` refuses to overwrite). `game_load_async(path)` reads and validates (magic, version,
size) on the IO lane into the same buffer; `game_io_poll()`, called once per frame by `mare.c`,
notices the join counter completing, and for a load swaps the state in and rebuilds the caches
right there - which is where the one real cost of a load sits: the nav rows, ~300 ms on 8
chunks. A save never blocks the frame; a load is one deliberate pause.

Rules that keep it simple: one operation in flight (a save requested while a save is writing is
queued and snapshots when it starts; a load is refused while anything is in flight, and a save
is refused while a load is); the buffer is owned by the IO job until the counter completes,
and the counter's acquire/release is the only synchronisation; the blocking `game_save` /
`game_load` remain for tests and tools and write the same bytes, which `sim_test` asserts.
Autosave is a `mare.c` policy (`AUTOSAVE_SECS` of unpaused play, `mare_autosave.bin`,
`MARE_AUTOSAVE_S` overrides) and uses nothing the manual save does not.

## Observability (step 10)

`jobs_stats()` fills a POD with, per lane: jobs submitted, executed, currently queued and busy
milliseconds, plus the worker and IO thread counts, the inline-run count and how long threads
blocked in `jobs_wait` with nothing to help with. `mare.c` publishes `jobs.main_ran`,
`jobs.queued`, `jobs.io_queued`, `jobs.wait_ms` and `jobs.busy_pct` (pool busy time over the
last half second divided by workers) as metrics, times the drain in the `jobs.upload` scope,
and the save path publishes `save.snapshot_ms`, `io.save_ms`, `io.load_ms` and `save.count`.
The dashboard **Jobs** tab shows the lanes as a table - threads, busy % over a half-second
window, queued now and peak, jobs/s, executed, busy total - over a queue-depth history graph,
then the asset pipeline's stages in flight (`asset_t.stage` is written by the stage that
finishes: IO read, DECODE build, MAIN commit pending, plus LODs still waiting for an upload
slot), the streamed textures, and the save/load state with the last save's two costs and the
autosave countdown. Any frame over 25 ms is logged at DEBUG with the previous frame's CPU
scope, GPU time, upload jobs, assets loading and save/load state, so a hitch in a run log is
attributed rather than guessed; `tools/metrics_summary.py` reduces a metrics CSV to the
median / p99 / max table used below.

Stalls are measured, not guessed: `jobs_wait` records how long the calling thread blocked with
nothing to help with, which is exactly the "stall during tick processing or world traversal"
the system exists to remove. In every capture below that total is 4-18 ms for the whole run,
all of it the main thread joining the start-up precompute.

Stalls are measured, not guessed: `jobs_wait` records how long the calling thread blocked with
nothing to help with, which is exactly the "stall during tick processing or world traversal" the
system exists to remove.

### Baseline, 0.8.1, before any client lands

Release build, 24-core Windows box, `--dev --skip=120`, 15 s per view, 1 authored asset
(`church`, 4 LODs, 128k triangles at LOD 0). This is what the later steps are measured against.

| | fully zoomed-out map (`MARE_CAM=-30,25,900`) | staged 50-ship battle (`MARE_DETAIL=battle`) |
|---|---|---|
| `frame.dt_ms` median / worst | 4.0 / **90.4** | 4.0 / **90.2** |
| `frame.cpu` avg / max | 0.265 / 0.764 ms | 0.165 / 0.963 ms |
| `gpu.total_ms` avg / max | 0.81 / 2.75 | 1.00 / 5.28 |
| draw calls / instances | 203 / 427 | 30 / 149 (peak 4175) |
| VRAM / RAM | 35.6 / 200 MB | 39.1 / 204 MB |
| `jobs.upload` | 0.001 ms avg | 0.001 ms avg |

Two things stand out and both are on the plan. The **90 ms worst frame** is startup, not
steady state: `church` alone costs 795 ms of synchronous glTF parse + texture decode before the
first frame, and the renderer initialises for another 38 ms (step 2 moves that off the render
thread). And the map view already spends more CPU than the battle does, on terrain, overlays
and icons walking every port and fleet at every zoom - which is what the LOD director and icon
clustering are for (steps 5 and 6).

### After, 0.8.3, every step landed

Same box (Intel integrated graphics, 240 Hz monitor so the limiter is off), Release, `--dev
--skip=120`, 20 s per view, **nine** authored assets (church + eight Meshy houses, 4 LODs each,
66 MB on the GPU), autosave forced every 4 s (`MARE_AUTOSAVE_S=4`). Steady-state rows exclude
the first 3 s; `tools/metrics_summary.py <csv> --after 3`.

| | fully zoomed-out map (`MARE_CAM=-30,25,900`) | staged 50-ship battle (`MARE_DETAIL=battle`) |
|---|---|---|
| `frame.dt_ms` median / worst after start-up | 4.5 / **6.1** (was 4.0 / 90.4) | 4.4 / **5.9** (was 4.0 / 90.2) |
| `frame.cpu` avg / max | 0.32 / 0.99 ms | 0.21 / 0.42 ms |
| `gpu.total_ms` avg / max | 0.91 / 1.55 | 1.81 / 2.69 |
| draw calls / instances | 203 / 353 | 31 / 146 |
| VRAM / RAM | 105.8 / 280 MB (9 assets resident) | 105.9 / 281 MB |
| `jobs.upload` | 0.00 ms avg | 0.00 ms avg |
| `jobs.wait_ms`, whole run | 4.2 ms (start-up join) | 9.4 ms |
| autosaves / worst frame while one was writing | 4 / **5.1 ms** | 4 / **5.1 ms** |
| save snapshot (main thread) / write (IO lane) | 0.09-0.33 ms / 3.0-5.1 ms | 0.10-0.27 ms / 2.8-4.5 ms |
| start-up: houses / church / nav rows | 365 ms concurrently / 931 ms / 307 ms | 967 ms concurrently / 968 ms / 301 ms |
| first frame (GL warm-up, unchanged) | 92.8 ms | 91.4 ms |

What changed in the numbers: the worst frame after start-up is the median plus a millisecond,
with nine assets instead of one resident and four saves written during each run. The one
start-up stall that remains is the first present (~92 ms), which no amount of threading moves.

**Scripted full-range zoom** (`MARE_ZOOM=6,900,12,-6.0,37.3,3`: Seville, 6 to 900 units and
back in 24 s after a 3 s hold, 29 s runs), once with autosave every 4 s and once with it off:

| | autosave every 4 s | autosave off |
|---|---|---|
| `frame.dt_ms` median / p99 / worst | 5.2 / 19.0 / 25.5 | 5.4 / 22.3 / 27.5 |
| `frame.cpu` median / max | 0.41 / 4.4 ms | 0.34 / 0.72 ms |
| frames over 25 ms | 26, all at 6-10 units | 29, all at 6-10 units (two at 40-45) |
| authored triangles max / director scale max | 2.16 M / 1.47 | 2.17 M / 1.49 |
| instances mid-fade max | 72 | 72 |
| autosaves / worst frame while writing | 7 / 4.0-14.1 ms | - |

The long frames are the city tier over Seville with ~2.1 M authored triangles at LOD 0 plus
their shadows on the integrated GPU (CPU 0.5 ms in each of those frames, the director already
at x1.47): the step-5 limit recorded in KNOWN_ISSUES, present identically with saving on and
off, so not a stall and not the save's. Elsewhere in the 24 s of motion, with ~70 instances
cross-fading at once, nothing crossed 25 ms. `lod.pops` (clamp activations) fired in two
places only: 37 in the 0.3 s after the church landed mid-run and the town's item list was
rebuilt around it, and single activations for ~0.1 s as the director's scale relaxed across
the city/battle boundary - each walked one level per fade, which is what the clamp is for.
