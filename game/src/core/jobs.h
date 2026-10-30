// Job system: the thread pool every long-running piece of work goes through (game/docs/DESIGN_JOBS.md).
//
// One worker pool of (cores - 1) threads serves several *lanes* rather than one pool per kind of
// work, so the machine is never oversubscribed:
//
//   JOB_LANE_WORKER   pool, high priority - tick phases, scene build, culling, LOD selection
//   JOB_LANE_DECODE   pool, low priority  - glTF parsing, texture decode, mesh processing
//   JOB_LANE_IO       dedicated threads   - reading/writing files (blocking syscalls)
//   JOB_LANE_MAIN     the render thread   - GPU upload and anything else touching GL
//
// A worker drains WORKER before DECODE, so a frame-critical parallel_for never queues behind an
// 800 ms texture decode. JOB_LANE_MAIN jobs are only ever run by the thread that called
// jobs_init (the one owning the GL context), when it calls jobs_main_drain or waits.
//
// This is the C API over mare::core::WorkerPool (core/worker_pool.hh / .cc). C++ code calls
// the class directly; unported C modules go through these shims.
//
// Rules worth knowing before using it:
//   * Waiting helps. jobs_wait and jobs_parallel_for run queued jobs on the calling thread while
//     they wait, so a job may wait on jobs it spawned without deadlocking.
//   * Submitting never allocates. Each lane is a fixed-capacity ring; submitting to a full one
//     runs the job on the calling thread (back-pressure instead of an untested failure path).
//   * Job arguments are owned by the caller and must outlive the job.
//   * parallel_for chunk boundaries depend only on (count, grain), never on the worker count, so
//     a reduction into per-chunk partials summed in chunk order is bit-identical however the
//     chunks were scheduled. This is what keeps the simulation deterministic (DESIGN_JOBS.md).

#ifndef MARE_JOBS_H
#define MARE_JOBS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum job_lane_t {
    JOB_LANE_WORKER = 0,
    JOB_LANE_DECODE,
    JOB_LANE_IO,
    JOB_LANE_MAIN,
    JOB_LANE_COUNT
} job_lane_t;

typedef unsigned job_counter_t;          // join handle; 0 = "no counter"
#define JOB_NO_COUNTER 0u

typedef void (*job_fn)(void *arg);
typedef void (*job_range_fn)(void *arg, int begin, int end, int slot);   // half-open [begin, end)

// lifecycle. Call jobs_init from the thread that owns the GL context, before anything submits.
//   nworkers < 0  auto: (cores - 1), capped, honouring the MARE_JOBS environment variable
//   nworkers == 0 synchronous: every submit runs inline on the calling thread (debugging,
//                 headless runs, determinism tests)
// Returns the number of worker threads actually started (0 = synchronous). Safe to call twice
// (the second call shuts the first pool down first). Before jobs_init, and after jobs_shutdown,
// the whole API behaves as if synchronous, so no call site needs a "are we initialised" check.
int  jobs_init(int nworkers);
void jobs_shutdown(void);                // drains what is queued, joins every thread
int  jobs_worker_count(void);

// Stable per-thread index in [0, jobs_slot_count()) for scratch and reduction buffers: the main
// thread is 0, pool workers and I/O threads have their own, everything else shares the last one.
int  jobs_slot(void);
int  jobs_slot_count(void);

// submission and joining
job_counter_t jobs_counter(void);        // allocate a join handle (0 if the pool ran out)
void jobs_submit(job_lane_t lane, job_fn fn, void *arg, job_counter_t counter);
void jobs_wait(job_counter_t counter);   // run queued jobs until it completes, then release it
int  jobs_done(job_counter_t counter);   // 1 when nothing is outstanding (does not release)
void jobs_release(job_counter_t counter);// give the handle back without waiting

// Run one queued job on the calling thread (MAIN jobs included when this is the render thread).
// Returns 1 if something ran; otherwise sleeps briefly and returns 0. For callers that wait on
// a state change rather than a counter (e.g. asset_load polling an asset's state).
int  jobs_help(void);

// Split [0, count) into ceil(count/grain) fixed chunks, run them across the lane and the calling
// thread, and return once all of them are done.
void jobs_parallel_for(job_lane_t lane, job_range_fn fn, void *arg, int count, int grain);

// Render thread, once per frame: run queued JOB_LANE_MAIN jobs until the queue is empty or
// `budget_ms` has elapsed (<= 0 means no limit). Returns how many ran.
int  jobs_main_drain(double budget_ms);

// statistics for the dashboard and the metrics CSV (cumulative since jobs_init)
typedef struct job_lane_stats_t {
    long long submitted, executed;
    double    busy_ms;                   // time spent inside job functions on this lane
    int       queued;                    // waiting right now
} job_lane_stats_t;

typedef struct job_stats_t {
    int       workers, slots;
    int       io_threads;                // dedicated IO lane threads (0 in synchronous mode)
    long long inline_runs;               // ran on the submitting thread (synchronous, or a full lane)
    double    wait_ms;                   // time blocked in jobs_wait with nothing to help with
    job_lane_stats_t lane[JOB_LANE_COUNT];
} job_stats_t;

void        jobs_stats(job_stats_t *out);
const char *jobs_lane_name(job_lane_t lane);

#ifdef __cplusplus
}
#endif

#endif
