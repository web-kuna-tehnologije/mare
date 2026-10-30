// mare::core::WorkerPool implementation (see worker_pool.hh for the API and
// game/docs/DESIGN_JOBS.md for the why). Behavior is the old jobs.cpp's, verbatim:
// the same queues, the same back-pressure, the same deterministic parallel_for.
//
// No exceptions are thrown, and no STL type crosses the C ABI (jobs.h) at the bottom.

#include "worker_pool.hh"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace mare::core {
namespace {

using pool_clock = std::chrono::steady_clock;

inline double ms_since(const pool_clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(pool_clock::now() - t0).count();
}

// The dense per-thread scratch index. Thread-local storage cannot be a non-static member;
// one pool exists per process (the singleton), so a single slot per thread is correct.
thread_local int tls_slot = -1;

} // namespace

WorkerPool &WorkerPool::instance() {
    static WorkerPool pool;
    return pool;
}

// ---------------------------------------------------------------------------
// counters

WorkerPool::CounterSlot *WorkerPool::slot_of(job_counter_t c) const {
    if (!c) return nullptr;
    unsigned idx = c & ((1u << IDX_BITS) - 1u), gen = c >> IDX_BITS;
    if (idx >= (unsigned)MAX_COUNTERS) return nullptr;
    const CounterSlot *s = &counters_[idx];
    return (s->in_use.load(std::memory_order_acquire) && s->gen.load(std::memory_order_relaxed) == gen) ? const_cast<CounterSlot *>(s) : nullptr;   // stale handle: ignore
}

void WorkerPool::counters_init() {
    std::lock_guard<std::mutex> lk(counter_mutex_);
    counter_nfree_ = 0;
    for (int i = MAX_COUNTERS - 1; i >= 0; --i) {
        counters_[i].pending.store(0, std::memory_order_relaxed);
        counters_[i].in_use.store(false, std::memory_order_relaxed);
        if (!counters_[i].gen.load(std::memory_order_relaxed)) counters_[i].gen.store(1, std::memory_order_relaxed);
        counter_free_[counter_nfree_++] = i;
    }
}

void WorkerPool::complete(job_counter_t c) {
    CounterSlot *s = slot_of(c);
    if (!s) return;
    if (s->pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        std::lock_guard<std::mutex> lk(done_mutex_);
        done_cv_.notify_all();
    }
}

job_counter_t WorkerPool::make_counter() {
    std::lock_guard<std::mutex> lk(counter_mutex_);
    if (!counter_nfree_) return JOB_NO_COUNTER;      // caller falls back to a blocking submit
    int idx = counter_free_[--counter_nfree_];
    CounterSlot *s = &counters_[idx];
    s->pending.store(0, std::memory_order_relaxed);
    s->in_use.store(true, std::memory_order_release);
    return (job_counter_t)((s->gen.load(std::memory_order_relaxed) << IDX_BITS) | (unsigned)idx);
}

void WorkerPool::release(job_counter_t c) {
    std::lock_guard<std::mutex> lk(counter_mutex_);
    CounterSlot *s = slot_of(c);
    if (!s) return;
    s->in_use.store(false, std::memory_order_release);
    unsigned g = s->gen.load(std::memory_order_relaxed) + 1;
    s->gen.store(g ? g : 1, std::memory_order_relaxed);   // generation 0 would make handle 0 reachable
    counter_free_[counter_nfree_++] = (int)(s - counters_);
}

bool WorkerPool::done(job_counter_t c) const {
    CounterSlot *s = slot_of(c);
    return !s || s->pending.load(std::memory_order_acquire) <= 0;
}

// ---------------------------------------------------------------------------
// running

void WorkerPool::run_job(const Job &j) {
    pool_clock::time_point t0 = pool_clock::now();
    j.fn(j.arg);
    busy_us_[j.lane].fetch_add((long long)(ms_since(t0) * 1000.0), std::memory_order_relaxed);
    executed_[j.lane].fetch_add(1, std::memory_order_relaxed);
    complete(j.counter);
}

// WORKER before DECODE: a frame-critical parallel_for must never queue behind a texture decode.
bool WorkerPool::pop_pool(Job &j) {
    std::lock_guard<std::mutex> lk(pool_mutex_);
    return q_worker_.pop(j) || q_decode_.pop(j);
}
bool WorkerPool::pop_main(Job &j) {
    std::lock_guard<std::mutex> lk(main_mutex_);
    return q_main_.pop(j);
}

// One unit of helping for a thread that is waiting. The main thread also takes MAIN jobs (it is
// the only thread allowed to), which is what keeps a wait on a chain ending in a GPU upload from
// deadlocking; workers never touch that lane.
bool WorkerPool::help_once(bool on_main) {
    Job j;
    if (pop_pool(j)) { run_job(j); return true; }
    if (on_main && pop_main(j)) { run_job(j); return true; }
    return false;
}

void WorkerPool::worker_main(int slot) {
    tls_slot = slot;
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lk(pool_mutex_);
            while (!q_worker_.pop(j) && !q_decode_.pop(j)) {
                if (quit_.load(std::memory_order_acquire)) return;   // queues empty and shutting down
                pool_cv_.wait(lk);
            }
        }
        run_job(j);
    }
}

void WorkerPool::io_main(int slot) {
    tls_slot = slot;
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lk(io_mutex_);
            while (!q_io_.pop(j)) {
                if (quit_.load(std::memory_order_acquire)) return;
                io_cv_.wait(lk);
            }
        }
        run_job(j);
    }
}

// ---------------------------------------------------------------------------
// lifecycle

int WorkerPool::start(int nworkers) {
    if (threaded_.load()) stop();

    if (nworkers < 0) {
        const char *env = getenv("MARE_JOBS");
        if (env && *env) nworkers = atoi(env);
        else {
            unsigned hw = std::thread::hardware_concurrency();
            nworkers = hw > 1 ? (int)hw - 1 : 0;     // leave a core for the render thread
        }
    }
    if (nworkers < 0) nworkers = 0;
    if (nworkers > MAX_WORKERS) nworkers = MAX_WORKERS;

    counters_init();
    for (int l = 0; l < JOB_LANE_COUNT; ++l) {
        submitted_[l].store(0); executed_[l].store(0); busy_us_[l].store(0);
    }
    inline_runs_.store(0); wait_us_.store(0);
    main_thread_ = std::this_thread::get_id();
    tls_slot = 0;                                    // the main thread is always slot 0
    quit_.store(false);
    nworkers_ = nworkers;
    nio_ = nworkers ? IO_THREADS : 0;
    slots_ = 1 + nworkers_ + nio_ + 1;               // main + pool + io + one shared overflow
    next_slot_.store(1 + nworkers_ + nio_);          // unknown threads land on the overflow slot
    if (!nworkers) { threaded_.store(false); return 0; }   // synchronous mode

    threaded_.store(true);
    for (int i = 0; i < nworkers_; ++i) workers_[i] = std::thread(&WorkerPool::worker_main, this, 1 + i);
    for (int i = 0; i < nio_; ++i)      io_[i]      = std::thread(&WorkerPool::io_main, this, 1 + nworkers_ + i);
    return nworkers_;
}

void WorkerPool::stop() {
    if (!threaded_.exchange(false)) return;
    quit_.store(true, std::memory_order_release);
    { std::lock_guard<std::mutex> lk(pool_mutex_); pool_cv_.notify_all(); }
    { std::lock_guard<std::mutex> lk(io_mutex_);   io_cv_.notify_all(); }
    for (int i = 0; i < nworkers_; ++i) if (workers_[i].joinable()) workers_[i].join();
    for (int i = 0; i < nio_; ++i)      if (io_[i].joinable())      io_[i].join();
    nworkers_ = nio_ = 0;
    // Anything that was queued when the threads went away still has to complete, or a thread
    // waiting on its counter would hang. Pool and I/O work is run here; queued MAIN jobs are
    // dropped (by the time the pool shuts down the GL context may be gone) but their counters
    // are completed all the same.
    for (;;) { Job j; if (!pop_pool(j)) break; run_job(j); }
    for (;;) { Job j; { std::lock_guard<std::mutex> lk(io_mutex_); if (!q_io_.pop(j)) break; } run_job(j); }
    for (;;) { Job j; if (!pop_main(j)) break; complete(j.counter); }
    { std::lock_guard<std::mutex> lk(done_mutex_); done_cv_.notify_all(); }
}

int WorkerPool::slot() {
    if (tls_slot < 0) {
        int s = next_slot_.fetch_add(1, std::memory_order_relaxed);
        if (s >= slots_) s = slots_ - 1;
        tls_slot = s;
    }
    return tls_slot;
}

// ---------------------------------------------------------------------------
// waiting and helping

void WorkerPool::wait(job_counter_t c) {
    CounterSlot *s = slot_of(c);
    if (!s) return;
    const bool main = on_main_thread();
    double idle_ms = 0;
    while (s->pending.load(std::memory_order_acquire) > 0) {
        if (help_once(main)) continue;
        // Nothing to help with: block until something completes. The short timeout covers the
        // window between the check above and the wait, and wake-ups from the I/O threads.
        pool_clock::time_point t0 = pool_clock::now();
        {
            std::unique_lock<std::mutex> lk(done_mutex_);
            done_cv_.wait_for(lk, std::chrono::microseconds(200));
        }
        idle_ms += ms_since(t0);
    }
    if (idle_ms > 0) wait_us_.fetch_add((long long)(idle_ms * 1000.0), std::memory_order_relaxed);
    release(c);
}

bool WorkerPool::help() {
    if (!threaded_.load(std::memory_order_acquire)) return false;
    if (help_once(on_main_thread())) return true;
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    return false;
}

// ---------------------------------------------------------------------------
// submission

void WorkerPool::submit(job_lane_t lane, job_fn fn, void *arg, job_counter_t counter) {
    if (!fn) return;
    if ((unsigned)lane >= JOB_LANE_COUNT) lane = JOB_LANE_WORKER;
    CounterSlot *s = slot_of(counter);
    if (s) s->pending.fetch_add(1, std::memory_order_relaxed);
    else   counter = JOB_NO_COUNTER;
    submitted_[lane].fetch_add(1, std::memory_order_relaxed);

    Job j; j.fn = fn; j.arg = arg; j.counter = counter; j.lane = lane;
    if (!threaded_.load(std::memory_order_acquire)) {      // synchronous mode / before start
        inline_runs_.fetch_add(1, std::memory_order_relaxed);
        run_job(j);
        return;
    }
    switch (lane) {
    case JOB_LANE_MAIN:
        // Only the render thread may run these, so a full queue cannot be resolved by running
        // the job here - wait for the next drain instead. (Unless we *are* the render thread,
        // in which case running it now is both legal and the only way to make progress.)
        for (;;) {
            { std::lock_guard<std::mutex> lk(main_mutex_); if (q_main_.push(j)) return; }
            if (on_main_thread()) { inline_runs_.fetch_add(1, std::memory_order_relaxed); run_job(j); return; }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    case JOB_LANE_IO:
        { std::lock_guard<std::mutex> lk(io_mutex_); if (q_io_.push(j)) { io_cv_.notify_one(); return; } }
        break;
    default:
        { std::lock_guard<std::mutex> lk(pool_mutex_);
          Queue &q = (lane == JOB_LANE_DECODE) ? q_decode_ : q_worker_;
          if (q.push(j)) { pool_cv_.notify_one(); return; } }
        break;
    }
    inline_runs_.fetch_add(1, std::memory_order_relaxed);  // lane full: back-pressure
    run_job(j);
}

namespace {
struct pf_state_t { job_range_fn fn; void *arg; std::atomic<int> next; int count, grain; };
} // namespace

void WorkerPool::parallel_entry(void *p) {
    pf_state_t *s = (pf_state_t *)p;
    const int slot = WorkerPool::instance().slot();
    for (;;) {
        int begin = s->next.fetch_add(s->grain, std::memory_order_relaxed);
        if (begin >= s->count) return;
        int end = begin + s->grain;
        if (end > s->count) end = s->count;
        s->fn(s->arg, begin, end, slot);
    }
}

void WorkerPool::parallel_for(job_lane_t lane, job_range_fn fn, void *arg, int count, int grain) {
    if (!fn || count <= 0) return;
    if (grain < 1) grain = 1;
    // Chunk boundaries are a function of (count, grain) only - never of the worker count - so a
    // reduction into per-chunk partials is bit-identical however the chunks got scheduled.
    if (!threaded_.load(std::memory_order_acquire) || lane == JOB_LANE_MAIN) {
        const int s = slot();
        for (int b = 0; b < count; b += grain) fn(arg, b, (b + grain > count) ? count : b + grain, s);
        return;
    }
    pf_state_t st;
    st.fn = fn; st.arg = arg; st.count = count; st.grain = grain;
    st.next.store(0, std::memory_order_relaxed);

    int chunks = (count + grain - 1) / grain;
    int helpers = chunks - 1;                        // the calling thread takes a share too
    if (helpers > nworkers_) helpers = nworkers_;
    job_counter_t c = helpers > 0 ? make_counter() : JOB_NO_COUNTER;
    if (!c) { parallel_entry(&st); return; }         // no counters left: run it all here
    for (int i = 0; i < helpers; ++i) submit(lane, parallel_entry, &st, c);
    parallel_entry(&st);
    wait(c);                                         // st is on this stack: must not return early
}

int WorkerPool::drain_main(double budget_ms) {
    if (!threaded_.load(std::memory_order_acquire)) return 0;
    pool_clock::time_point t0 = pool_clock::now();
    int n = 0;
    for (;;) {
        Job j;
        if (!pop_main(j)) break;
        run_job(j);
        ++n;
        if (budget_ms > 0 && ms_since(t0) >= budget_ms) break;
    }
    return n;
}

// ---------------------------------------------------------------------------
// statistics

void WorkerPool::stats(job_stats_t *out) const {
    if (!out) return;
    memset(out, 0, sizeof *out);
    out->workers = nworkers_;
    out->slots = slots_;
    out->io_threads = nio_;
    out->inline_runs = inline_runs_.load(std::memory_order_relaxed);
    out->wait_ms = wait_us_.load(std::memory_order_relaxed) / 1000.0;
    for (int l = 0; l < JOB_LANE_COUNT; ++l) {
        out->lane[l].submitted = submitted_[l].load(std::memory_order_relaxed);
        out->lane[l].executed  = executed_[l].load(std::memory_order_relaxed);
        out->lane[l].busy_ms   = busy_us_[l].load(std::memory_order_relaxed) / 1000.0;
    }
    { std::lock_guard<std::mutex> lk(pool_mutex_);
      out->lane[JOB_LANE_WORKER].queued = q_worker_.size();
      out->lane[JOB_LANE_DECODE].queued = q_decode_.size(); }
    { std::lock_guard<std::mutex> lk(io_mutex_);   out->lane[JOB_LANE_IO].queued   = q_io_.size(); }
    { std::lock_guard<std::mutex> lk(main_mutex_); out->lane[JOB_LANE_MAIN].queued = q_main_.size(); }
}

} // namespace mare::core

// ---------------------------------------------------------------------------
// C ABI (jobs.h): thin shims over the WorkerPool singleton for unported C modules

using mare::core::WorkerPool;

extern "C" {

int  jobs_init(int nworkers) { return WorkerPool::instance().start(nworkers); }
void jobs_shutdown(void) { WorkerPool::instance().stop(); }
int  jobs_worker_count(void) { return WorkerPool::instance().worker_count(); }
int  jobs_slot_count(void) { return WorkerPool::instance().slot_count(); }
int  jobs_slot(void) { return WorkerPool::instance().slot(); }

const char *jobs_lane_name(job_lane_t lane) {
    static const char *names[JOB_LANE_COUNT] = { "worker", "decode", "io", "main" };
    return (unsigned)lane < JOB_LANE_COUNT ? names[lane] : "?";
}

job_counter_t jobs_counter(void) { return WorkerPool::instance().make_counter(); }
void jobs_release(job_counter_t c) { WorkerPool::instance().release(c); }
int  jobs_done(job_counter_t c) { return WorkerPool::instance().done(c); }
void jobs_wait(job_counter_t c) { WorkerPool::instance().wait(c); }
int  jobs_help(void) { return WorkerPool::instance().help(); }

void jobs_submit(job_lane_t lane, job_fn fn, void *arg, job_counter_t counter) {
    WorkerPool::instance().submit(lane, fn, arg, counter);
}
void jobs_parallel_for(job_lane_t lane, job_range_fn fn, void *arg, int count, int grain) {
    WorkerPool::instance().parallel_for(lane, fn, arg, count, grain);
}
int jobs_main_drain(double budget_ms) { return WorkerPool::instance().drain_main(budget_ms); }
void jobs_stats(job_stats_t *out) { WorkerPool::instance().stats(out); }

} // extern "C"
