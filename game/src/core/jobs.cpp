// Job system implementation - see jobs.h for the API and game/docs/DESIGN_JOBS.md for the why.
//
// This is the project's only C++ translation unit. It exists because a portable thread pool in
// C11 means several hundred lines of pthread/Win32 wrapper (MSVC has no <threads.h>), while
// C++17 gives us <thread>, <mutex>, <condition_variable> and <atomic> on all three toolchains
// the project already requires. Nothing C++ escapes this file: the header is a C ABI, no
// exceptions are thrown, and no STL type appears in a game structure.
//
// Structure:
//   * two queues (WORKER, DECODE) served by one worker pool under a single mutex + condvar.
//     WORKER is drained first. A single lock is deliberate: jobs here are coarse (a chunk of
//     ports, one glTF parse), so contention is irrelevant and the code stays easy to reason
//     about. If profiling ever says otherwise, the step is per-worker deques with stealing.
//   * one queue (IO) served by dedicated threads, because blocking a pool thread on a disk read
//     takes a core away from the frame.
//   * one queue (MAIN) that only the thread which called jobs_init ever runs, from
//     jobs_main_drain (budgeted, once per frame) or while it waits in jobs_wait.
//
// Counters are handles into a fixed slab (generation-tagged, so a stale handle is ignored rather
// than corrupting someone else's join), never pointers.

#include "jobs.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace {

enum {
    JOB_QUEUE_CAP    = 4096,   // per lane; a full lane runs the job on the submitting thread
    JOB_MAX_COUNTERS = 1024,
    JOB_IO_THREADS   = 2,
    JOB_MAX_WORKERS  = 64,
    JOB_IDX_BITS     = 12      // counter handle = (generation << 12) | slab index
};

typedef std::chrono::steady_clock clock_t_;

struct job_t { job_fn fn; void *arg; job_counter_t counter; int lane; };

// Fixed ring. Indices are free-running unsigned, so wrap-around is well defined; the owning
// mutex is always held by the caller.
struct queue_t {
    job_t    ring[JOB_QUEUE_CAP];
    unsigned head, tail;
    queue_t() : head(0), tail(0) {}
    int  size() const { return (int)(tail - head); }
    bool push(const job_t &j) {
        if ((unsigned)(tail - head) >= (unsigned)JOB_QUEUE_CAP) return false;
        ring[tail % JOB_QUEUE_CAP] = j; ++tail; return true;
    }
    bool pop(job_t &j) {
        if (head == tail) return false;
        j = ring[head % JOB_QUEUE_CAP]; ++head; return true;
    }
};

// gen/in_use are atomic so that resolving a handle never races with another thread allocating or
// releasing a different handle in the same slab.
struct counter_slot_t { std::atomic<int> pending; std::atomic<unsigned> gen; std::atomic<bool> in_use; };

// ---------------------------------------------------------------------------
// state

std::mutex              g_pool_mutex;          // guards q_worker + q_decode
std::condition_variable g_pool_cv;
queue_t                 g_q_worker, g_q_decode;

std::mutex              g_io_mutex;
std::condition_variable g_io_cv;
queue_t                 g_q_io;

std::mutex              g_main_mutex;          // guards q_main (no condvar: drained by polling)
queue_t                 g_q_main;

std::mutex              g_done_mutex;          // waiters block here when there is nothing to help with
std::condition_variable g_done_cv;

std::mutex       g_counter_mutex;
counter_slot_t   g_counters[JOB_MAX_COUNTERS];
int              g_counter_free[JOB_MAX_COUNTERS];
int              g_counter_nfree = 0;

std::thread      g_workers[JOB_MAX_WORKERS];
std::thread      g_io[JOB_IO_THREADS];
int              g_nworkers = 0, g_nio = 0;
std::atomic<bool> g_threaded(false);           // false before init, after shutdown, and in synchronous mode
std::atomic<bool> g_quit(false);
std::thread::id  g_main_thread;

int                   g_slots = 2;
std::atomic<int>      g_next_slot(0);
thread_local int      tls_slot = -1;

std::atomic<long long> g_submitted[JOB_LANE_COUNT];
std::atomic<long long> g_executed[JOB_LANE_COUNT];
std::atomic<long long> g_busy_us[JOB_LANE_COUNT];
std::atomic<long long> g_inline_runs(0);
std::atomic<long long> g_wait_us(0);

inline double ms_since(const clock_t_::time_point &t0) {
    return std::chrono::duration<double, std::milli>(clock_t_::now() - t0).count();
}

// ---------------------------------------------------------------------------
// counters

counter_slot_t *slot_of(job_counter_t c) {
    if (!c) return NULL;
    unsigned idx = c & ((1u << JOB_IDX_BITS) - 1u), gen = c >> JOB_IDX_BITS;
    if (idx >= (unsigned)JOB_MAX_COUNTERS) return NULL;
    counter_slot_t *s = &g_counters[idx];
    return (s->in_use.load(std::memory_order_acquire) && s->gen.load(std::memory_order_relaxed) == gen) ? s : NULL;   // stale handle: ignore
}

void counters_init() {
    std::lock_guard<std::mutex> lk(g_counter_mutex);
    g_counter_nfree = 0;
    for (int i = JOB_MAX_COUNTERS - 1; i >= 0; --i) {
        g_counters[i].pending.store(0, std::memory_order_relaxed);
        g_counters[i].in_use.store(false, std::memory_order_relaxed);
        if (!g_counters[i].gen.load(std::memory_order_relaxed)) g_counters[i].gen.store(1, std::memory_order_relaxed);
        g_counter_free[g_counter_nfree++] = i;
    }
}

void job_complete(job_counter_t c) {
    counter_slot_t *s = slot_of(c);
    if (!s) return;
    if (s->pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lk(g_done_mutex);
        g_done_cv.notify_all();
    }
}

// ---------------------------------------------------------------------------
// running

void run_job(const job_t &j) {
    clock_t_::time_point t0 = clock_t_::now();
    j.fn(j.arg);
    g_busy_us[j.lane].fetch_add((long long)(ms_since(t0) * 1000.0), std::memory_order_relaxed);
    g_executed[j.lane].fetch_add(1, std::memory_order_relaxed);
    job_complete(j.counter);
}

// WORKER before DECODE: a frame-critical parallel_for must never queue behind a texture decode.
bool pop_pool(job_t &j) {
    std::lock_guard<std::mutex> lk(g_pool_mutex);
    return g_q_worker.pop(j) || g_q_decode.pop(j);
}
bool pop_main(job_t &j) {
    std::lock_guard<std::mutex> lk(g_main_mutex);
    return g_q_main.pop(j);
}

// One unit of helping for a thread that is waiting. The main thread also takes MAIN jobs (it is
// the only thread allowed to), which is what keeps a wait on a chain ending in a GPU upload from
// deadlocking; workers never touch that lane.
bool help_once(bool on_main) {
    job_t j;
    if (pop_pool(j)) { run_job(j); return true; }
    if (on_main && pop_main(j)) { run_job(j); return true; }
    return false;
}

void worker_main(int slot) {
    tls_slot = slot;
    for (;;) {
        job_t j;
        {
            std::unique_lock<std::mutex> lk(g_pool_mutex);
            while (!g_q_worker.pop(j) && !g_q_decode.pop(j)) {
                if (g_quit.load(std::memory_order_acquire)) return;   // queues empty and shutting down
                g_pool_cv.wait(lk);
            }
        }
        run_job(j);
    }
}

void io_main(int slot) {
    tls_slot = slot;
    for (;;) {
        job_t j;
        {
            std::unique_lock<std::mutex> lk(g_io_mutex);
            while (!g_q_io.pop(j)) {
                if (g_quit.load(std::memory_order_acquire)) return;
                g_io_cv.wait(lk);
            }
        }
        run_job(j);
    }
}

bool on_main_thread() { return std::this_thread::get_id() == g_main_thread; }

}   // namespace

// ---------------------------------------------------------------------------
// lifecycle

extern "C" int jobs_init(int nworkers) {
    if (g_threaded.load()) jobs_shutdown();

    if (nworkers < 0) {
        const char *env = getenv("MARE_JOBS");
        if (env && *env) nworkers = atoi(env);
        else {
            unsigned hw = std::thread::hardware_concurrency();
            nworkers = hw > 1 ? (int)hw - 1 : 0;           // leave a core for the render thread
        }
    }
    if (nworkers < 0) nworkers = 0;
    if (nworkers > JOB_MAX_WORKERS) nworkers = JOB_MAX_WORKERS;

    counters_init();
    for (int l = 0; l < JOB_LANE_COUNT; ++l) {
        g_submitted[l].store(0); g_executed[l].store(0); g_busy_us[l].store(0);
    }
    g_inline_runs.store(0); g_wait_us.store(0);
    g_main_thread = std::this_thread::get_id();
    tls_slot = 0;                                           // the main thread is always slot 0
    g_quit.store(false);
    g_nworkers = nworkers;
    g_nio = nworkers ? JOB_IO_THREADS : 0;
    g_slots = 1 + g_nworkers + g_nio + 1;                   // main + pool + io + one shared overflow
    g_next_slot.store(1 + g_nworkers + g_nio);              // unknown threads land on the overflow slot
    if (!nworkers) { g_threaded.store(false); return 0; }   // synchronous mode

    g_threaded.store(true);
    for (int i = 0; i < g_nworkers; ++i) g_workers[i] = std::thread(worker_main, 1 + i);
    for (int i = 0; i < g_nio; ++i)      g_io[i]      = std::thread(io_main, 1 + g_nworkers + i);
    return g_nworkers;
}

extern "C" void jobs_shutdown(void) {
    if (!g_threaded.exchange(false)) return;
    g_quit.store(true, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(g_pool_mutex); g_pool_cv.notify_all(); }
    { std::lock_guard<std::mutex> lk(g_io_mutex);   g_io_cv.notify_all(); }
    for (int i = 0; i < g_nworkers; ++i) if (g_workers[i].joinable()) g_workers[i].join();
    for (int i = 0; i < g_nio; ++i)      if (g_io[i].joinable())      g_io[i].join();
    g_nworkers = g_nio = 0;
    // Anything that was queued when the threads went away still has to complete, or a thread
    // waiting on its counter would hang. Pool and I/O work is run here; queued MAIN jobs are
    // dropped (by the time the pool shuts down the GL context may be gone) but their counters
    // are completed all the same.
    for (;;) { job_t j; if (!pop_pool(j)) break; run_job(j); }
    for (;;) { job_t j; { std::lock_guard<std::mutex> lk(g_io_mutex); if (!g_q_io.pop(j)) break; } run_job(j); }
    for (;;) { job_t j; if (!pop_main(j)) break; job_complete(j.counter); }
    { std::lock_guard<std::mutex> lk(g_done_mutex); g_done_cv.notify_all(); }
}

extern "C" int jobs_worker_count(void) { return g_nworkers; }
extern "C" int jobs_slot_count(void) { return g_slots; }

extern "C" int jobs_slot(void) {
    if (tls_slot < 0) {
        int s = g_next_slot.fetch_add(1, std::memory_order_relaxed);
        if (s >= g_slots) s = g_slots - 1;
        tls_slot = s;
    }
    return tls_slot;
}

extern "C" const char *jobs_lane_name(job_lane_t lane) {
    static const char *names[JOB_LANE_COUNT] = { "worker", "decode", "io", "main" };
    return (unsigned)lane < JOB_LANE_COUNT ? names[lane] : "?";
}

// ---------------------------------------------------------------------------
// counters

extern "C" job_counter_t jobs_counter(void) {
    std::lock_guard<std::mutex> lk(g_counter_mutex);
    if (!g_counter_nfree) return JOB_NO_COUNTER;            // caller falls back to a blocking submit
    int idx = g_counter_free[--g_counter_nfree];
    counter_slot_t *s = &g_counters[idx];
    s->pending.store(0, std::memory_order_relaxed);
    s->in_use.store(true, std::memory_order_release);
    return (job_counter_t)((s->gen.load(std::memory_order_relaxed) << JOB_IDX_BITS) | (unsigned)idx);
}

extern "C" void jobs_release(job_counter_t c) {
    std::lock_guard<std::mutex> lk(g_counter_mutex);
    counter_slot_t *s = slot_of(c);
    if (!s) return;
    s->in_use.store(false, std::memory_order_release);
    unsigned g = s->gen.load(std::memory_order_relaxed) + 1;
    s->gen.store(g ? g : 1, std::memory_order_relaxed);     // generation 0 would make handle 0 reachable
    g_counter_free[g_counter_nfree++] = (int)(s - g_counters);
}

extern "C" int jobs_done(job_counter_t c) {
    counter_slot_t *s = slot_of(c);
    return !s || s->pending.load(std::memory_order_acquire) <= 0;
}

extern "C" void jobs_wait(job_counter_t c) {
    counter_slot_t *s = slot_of(c);
    if (!s) return;
    const bool main = on_main_thread();
    double idle_ms = 0;
    while (s->pending.load(std::memory_order_acquire) > 0) {
        if (help_once(main)) continue;
        // Nothing to help with: block until something completes. The short timeout covers the
        // window between the check above and the wait, and wake-ups from the I/O threads.
        clock_t_::time_point t0 = clock_t_::now();
        {
            std::unique_lock<std::mutex> lk(g_done_mutex);
            g_done_cv.wait_for(lk, std::chrono::microseconds(200));
        }
        idle_ms += ms_since(t0);
    }
    if (idle_ms > 0) g_wait_us.fetch_add((long long)(idle_ms * 1000.0), std::memory_order_relaxed);
    jobs_release(c);
}

extern "C" int jobs_help(void) {
    if (!g_threaded.load(std::memory_order_acquire)) return 0;
    if (help_once(on_main_thread())) return 1;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    return 0;
}

// ---------------------------------------------------------------------------
// submission

extern "C" void jobs_submit(job_lane_t lane, job_fn fn, void *arg, job_counter_t counter) {
    if (!fn) return;
    if ((unsigned)lane >= JOB_LANE_COUNT) lane = JOB_LANE_WORKER;
    counter_slot_t *s = slot_of(counter);
    if (s) s->pending.fetch_add(1, std::memory_order_relaxed);
    else   counter = JOB_NO_COUNTER;
    g_submitted[lane].fetch_add(1, std::memory_order_relaxed);

    job_t j; j.fn = fn; j.arg = arg; j.counter = counter; j.lane = lane;
    if (!g_threaded.load(std::memory_order_acquire)) {      // synchronous mode / before init
        g_inline_runs.fetch_add(1, std::memory_order_relaxed);
        run_job(j);
        return;
    }
    switch (lane) {
    case JOB_LANE_MAIN:
        // Only the render thread may run these, so a full queue cannot be resolved by running
        // the job here - wait for the next drain instead. (Unless we *are* the render thread,
        // in which case running it now is both legal and the only way to make progress.)
        for (;;) {
            { std::lock_guard<std::mutex> lk(g_main_mutex); if (g_q_main.push(j)) return; }
            if (on_main_thread()) { g_inline_runs.fetch_add(1, std::memory_order_relaxed); run_job(j); return; }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    case JOB_LANE_IO:
        { std::lock_guard<std::mutex> lk(g_io_mutex); if (g_q_io.push(j)) { g_io_cv.notify_one(); return; } }
        break;
    default:
        { std::lock_guard<std::mutex> lk(g_pool_mutex);
          queue_t &q = (lane == JOB_LANE_DECODE) ? g_q_decode : g_q_worker;
          if (q.push(j)) { g_pool_cv.notify_one(); return; } }
        break;
    }
    g_inline_runs.fetch_add(1, std::memory_order_relaxed);  // lane full: back-pressure
    run_job(j);
}

namespace {
struct pf_state_t { job_range_fn fn; void *arg; std::atomic<int> next; int count, grain; };

void pf_entry(void *p) {
    pf_state_t *s = (pf_state_t *)p;
    const int slot = jobs_slot();
    for (;;) {
        int begin = s->next.fetch_add(s->grain, std::memory_order_relaxed);
        if (begin >= s->count) return;
        int end = begin + s->grain;
        if (end > s->count) end = s->count;
        s->fn(s->arg, begin, end, slot);
    }
}
}   // namespace

extern "C" void jobs_parallel_for(job_lane_t lane, job_range_fn fn, void *arg, int count, int grain) {
    if (!fn || count <= 0) return;
    if (grain < 1) grain = 1;
    // Chunk boundaries are a function of (count, grain) only - never of the worker count - so a
    // reduction into per-chunk partials is bit-identical however the chunks got scheduled.
    if (!g_threaded.load(std::memory_order_acquire) || lane == JOB_LANE_MAIN) {
        const int slot = jobs_slot();
        for (int b = 0; b < count; b += grain) fn(arg, b, (b + grain > count) ? count : b + grain, slot);
        return;
    }
    pf_state_t st;
    st.fn = fn; st.arg = arg; st.count = count; st.grain = grain;
    st.next.store(0, std::memory_order_relaxed);

    int chunks = (count + grain - 1) / grain;
    int helpers = chunks - 1;                                // the calling thread takes a share too
    if (helpers > g_nworkers) helpers = g_nworkers;
    job_counter_t c = helpers > 0 ? jobs_counter() : JOB_NO_COUNTER;
    if (!c) { pf_entry(&st); return; }                       // no counters left: run it all here
    for (int i = 0; i < helpers; ++i) jobs_submit(lane, pf_entry, &st, c);
    pf_entry(&st);
    jobs_wait(c);                                            // st is on this stack: must not return early
}

extern "C" int jobs_main_drain(double budget_ms) {
    if (!g_threaded.load(std::memory_order_acquire)) return 0;
    clock_t_::time_point t0 = clock_t_::now();
    int n = 0;
    for (;;) {
        job_t j;
        if (!pop_main(j)) break;
        run_job(j);
        ++n;
        if (budget_ms > 0 && ms_since(t0) >= budget_ms) break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// statistics

extern "C" void jobs_stats(job_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->workers = g_nworkers;
    out->slots = g_slots;
    out->io_threads = g_nio;
    out->inline_runs = g_inline_runs.load(std::memory_order_relaxed);
    out->wait_ms = g_wait_us.load(std::memory_order_relaxed) / 1000.0;
    for (int l = 0; l < JOB_LANE_COUNT; ++l) {
        out->lane[l].submitted = g_submitted[l].load(std::memory_order_relaxed);
        out->lane[l].executed  = g_executed[l].load(std::memory_order_relaxed);
        out->lane[l].busy_ms   = g_busy_us[l].load(std::memory_order_relaxed) / 1000.0;
    }
    { std::lock_guard<std::mutex> lk(g_pool_mutex);
      out->lane[JOB_LANE_WORKER].queued = g_q_worker.size();
      out->lane[JOB_LANE_DECODE].queued = g_q_decode.size(); }
    { std::lock_guard<std::mutex> lk(g_io_mutex);   out->lane[JOB_LANE_IO].queued   = g_q_io.size(); }
    { std::lock_guard<std::mutex> lk(g_main_mutex); out->lane[JOB_LANE_MAIN].queued = g_q_main.size(); }
}
