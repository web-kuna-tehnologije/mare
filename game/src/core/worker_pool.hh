// mare::core::WorkerPool - the thread pool every long-running piece of work goes through.
// C++ face of the job system; the C ABI in jobs.h is a set of shims over the singleton
// (see game/docs/DESIGN_JOBS.md for the design and jobs.h for the lane semantics).
//
// One pool of (cores - 1) threads serves the WORKER and DECODE lanes (WORKER drained first),
// dedicated threads serve IO, and the MAIN lane is only ever run by the thread that called
// start() (the one owning the GL context), from drain_main() or while it waits.
//
// Rules worth knowing (unchanged from the C days):
//   * Waiting helps: wait() and parallel_for() run queued jobs on the calling thread.
//   * Submitting never allocates: a full lane runs the job on the calling thread.
//   * Job arguments are owned by the caller and must outlive the job.
//   * parallel_for chunk boundaries depend only on (count, grain), never on the worker count,
//     which is what keeps reductions - and the simulation - bit-deterministic.

#ifndef MARE_CORE_WORKER_POOL_HH
#define MARE_CORE_WORKER_POOL_HH

#include "jobs.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace mare::core {

class WorkerPool {
public:
    static WorkerPool &instance();

    WorkerPool() = default;
    ~WorkerPool() { stop(); }                        // RAII: a forgotten shutdown still joins
    WorkerPool(const WorkerPool &) = delete;
    WorkerPool &operator=(const WorkerPool &) = delete;

    // nworkers < 0: MARE_JOBS env or (hardware cores - 1); 0 = synchronous mode.
    // Returns the worker count. The calling thread becomes the main/render thread.
    int  start(int nworkers = -1);
    void stop();                                     // joins, drains pool/IO, completes MAIN counters

    int  worker_count() const { return nworkers_; }
    int  slot_count() const { return slots_; }
    int  slot();                                     // this thread's dense scratch index

    job_counter_t make_counter();                    // 0 if the slab is exhausted
    void release(job_counter_t c);
    bool done(job_counter_t c) const;
    void wait(job_counter_t c);                      // helps while waiting, then releases c

    void submit(job_lane_t lane, job_fn fn, void *arg, job_counter_t counter = JOB_NO_COUNTER);
    void parallel_for(job_lane_t lane, job_range_fn fn, void *arg, int count, int grain);

    // C++ convenience: f(begin, end, slot) over [0, count) - same determinism contract
    template <class F>
    void parallel_for(job_lane_t lane, int count, int grain, F &&f) {
        struct call_t { F *f; } call = { &f };
        parallel_for(lane, [](void *arg, int begin, int end, int s) { (*static_cast<call_t *>(arg)->f)(begin, end, s); }, &call, count, grain);
    }

    bool help();                                     // run one queued job if any (false = slept)
    int  drain_main(double budget_ms);               // main thread only: run MAIN-lane jobs
    void stats(job_stats_t *out) const;
    bool on_main_thread() const { return std::this_thread::get_id() == main_thread_; }

    // capacity contract (tests exercise the back-pressure paths at these limits)
    enum {
        QUEUE_CAP    = 4096,   // per lane; a full lane runs the job on the submitting thread
        MAX_COUNTERS = 1024,
        IO_THREADS   = 2,
        MAX_WORKERS  = 64,
        IDX_BITS     = 12      // counter handle = (generation << 12) | slab index
    };

private:
    struct Job { job_fn fn; void *arg; job_counter_t counter; int lane; };

    // Fixed ring. Indices are free-running unsigned, so wrap-around is well defined; the
    // owning mutex is always held by the caller.
    struct Queue {
        Job      ring[QUEUE_CAP];
        unsigned head = 0, tail = 0;
        int  size() const { return (int)(tail - head); }
        bool push(const Job &j) {
            if ((unsigned)(tail - head) >= (unsigned)QUEUE_CAP) return false;
            ring[tail % QUEUE_CAP] = j; ++tail; return true;
        }
        bool pop(Job &j) {
            if (head == tail) return false;
            j = ring[head % QUEUE_CAP]; ++head; return true;
        }
    };

    // gen/in_use are atomic so that resolving a handle never races with another thread
    // allocating or releasing a different handle in the same slab.
    struct CounterSlot { std::atomic<int> pending; std::atomic<unsigned> gen; std::atomic<bool> in_use; };

    CounterSlot *slot_of(job_counter_t c) const;
    void counters_init();
    void complete(job_counter_t c);
    void run_job(const Job &j);
    bool pop_pool(Job &j);
    bool pop_main(Job &j);
    bool help_once(bool on_main);
    void worker_main(int slot);
    void io_main(int slot);
    static void parallel_entry(void *p);

    mutable std::mutex      pool_mutex_;             // guards q_worker_ + q_decode_
    std::condition_variable pool_cv_;
    Queue                   q_worker_, q_decode_;

    mutable std::mutex      io_mutex_;
    std::condition_variable io_cv_;
    Queue                   q_io_;

    mutable std::mutex      main_mutex_;             // guards q_main_ (no condvar: polled)
    Queue                   q_main_;

    std::mutex              done_mutex_;             // waiters block here when there is nothing to help with
    std::condition_variable done_cv_;

    std::mutex   counter_mutex_;
    CounterSlot  counters_[MAX_COUNTERS];
    int          counter_free_[MAX_COUNTERS];
    int          counter_nfree_ = 0;

    std::thread  workers_[MAX_WORKERS];
    std::thread  io_[IO_THREADS];
    int          nworkers_ = 0, nio_ = 0;
    std::atomic<bool> threaded_{false};              // false before start, after stop, in sync mode
    std::atomic<bool> quit_{false};
    std::thread::id   main_thread_;

    int              slots_ = 2;
    std::atomic<int> next_slot_{0};

    std::atomic<long long> submitted_[JOB_LANE_COUNT] = {};
    std::atomic<long long> executed_[JOB_LANE_COUNT] = {};
    std::atomic<long long> busy_us_[JOB_LANE_COUNT] = {};
    std::atomic<long long> inline_runs_{0};
    std::atomic<long long> wait_us_{0};
};

} // namespace mare::core

#endif
