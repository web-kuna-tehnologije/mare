# Design: job system (multithreading)

Status: **steps 1 and 2 done** - `core/jobs.h` + `core/jobs.cpp` and `tests/jobs_test.cpp`
exist, the pool starts with the game, the render thread drains the MAIN lane every frame, and
the asset runtime (`assets/asset.c`) and the streamed textures (`gfx/textures.c`) load through
the lanes exactly as the pipeline below describes. Steps 7-9 of the plan in `docs/ROADMAP.md`
(LOD streaming, the tick, save/load) are the remaining clients.

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

## Observability

`jobs_stats()` fills a POD with, per lane: jobs submitted, executed, currently queued and busy
milliseconds, plus the inline-run count and how long threads blocked in `jobs_wait` with nothing
to help with. Today `mare.c` publishes `jobs.main_ran` and `jobs.queued` as metrics and times
the drain in the `jobs.upload` profiler scope (0.001 ms with an idle pool). Step 10 adds the
dashboard **Jobs** tab: lane occupancy bars, queue depths and the asset pipeline's in-flight
stages.

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
