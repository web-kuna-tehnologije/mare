// Headless job-system test (core/jobs.cpp, see game/docs/DESIGN_JOBS.md): lanes, join counters,
// parallel_for coverage and chunk determinism, main-thread drain and its budget, back-pressure
// when a lane is full, nested waits, and synchronous mode. No framework, no GL.
//
//   c++ -std=c++17 game/tests/jobs_test.cpp -o jobs_test -O2 -Wall -pthread && ./jobs_test

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

#include "../src/core/worker_pool.cc"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static std::thread::id main_id;

// --------------------------------------------------------------------------
// helpers

static std::atomic<int> g_counter_hits(0);
static void bump(void *arg) { (void)arg; g_counter_hits.fetch_add(1); }

struct range_check_t { std::vector<int> *hits; std::atomic<int> *bad_slot; };

static void range_mark(void *arg, int begin, int end, int slot) {
    range_check_t *rc = (range_check_t *)arg;
    if (slot < 0 || slot >= jobs_slot_count()) rc->bad_slot->fetch_add(1);
    for (int i = begin; i < end; ++i) (*rc->hits)[i]++;
}

// per-chunk partials: summed in chunk order afterwards, so the result must not depend on how the
// chunks were scheduled (the determinism rule in DESIGN_JOBS.md)
struct reduce_t { const double *v; double *partial; int grain; };
static void range_reduce(void *arg, int begin, int end, int slot) {
    (void)slot;
    reduce_t *r = (reduce_t *)arg;
    double s = 0;
    for (int i = begin; i < end; ++i) s += r->v[i];
    r->partial[begin / r->grain] = s;
}

static double reduce_run(const double *v, int n, int grain) {
    std::vector<double> partial((size_t)((n + grain - 1) / grain), 0.0);
    reduce_t r = { v, partial.data(), grain };
    jobs_parallel_for(JOB_LANE_WORKER, range_reduce, &r, n, grain);
    double total = 0;
    for (size_t i = 0; i < partial.size(); ++i) total += partial[i];
    return total;
}

// --------------------------------------------------------------------------
// tests

static void test_synchronous(void) {
    int n = jobs_init(0);
    CHECK(n == 0, "synchronous mode should report 0 workers, got %d", n);
    CHECK(jobs_worker_count() == 0, "worker count in synchronous mode");

    g_counter_hits.store(0);
    job_counter_t c = jobs_counter();
    CHECK(c != JOB_NO_COUNTER, "counter allocation");
    for (int i = 0; i < 16; ++i) jobs_submit(JOB_LANE_WORKER, bump, NULL, c);
    CHECK(g_counter_hits.load() == 16, "synchronous submit runs inline: %d of 16", g_counter_hits.load());
    CHECK(jobs_done(c), "counter is complete after inline runs");
    jobs_wait(c);

    std::vector<int> hits(1000, 0);
    std::atomic<int> bad(0);
    range_check_t rc = { &hits, &bad };
    jobs_parallel_for(JOB_LANE_WORKER, range_mark, &rc, 1000, 64);
    int once = 0;
    for (int i = 0; i < 1000; ++i) once += (hits[i] == 1);
    CHECK(once == 1000, "synchronous parallel_for covers every index once: %d of 1000", once);

    // a MAIN job runs inline too, so nothing can be stranded in a headless run
    g_counter_hits.store(0);
    jobs_submit(JOB_LANE_MAIN, bump, NULL, JOB_NO_COUNTER);
    CHECK(g_counter_hits.load() == 1, "synchronous MAIN submit runs inline");
    CHECK(jobs_main_drain(0) == 0, "nothing left to drain in synchronous mode");
    jobs_shutdown();
}

static void test_lanes(void) {
    int n = jobs_init(4);
    CHECK(n == 4, "four workers requested, got %d", n);
    CHECK(jobs_slot_count() >= 4, "slot count covers the pool: %d", jobs_slot_count());
    CHECK(jobs_slot() == 0, "the thread that called jobs_init is slot 0");

    g_counter_hits.store(0);
    job_counter_t c = jobs_counter();
    for (int i = 0; i < 200; ++i) jobs_submit(JOB_LANE_WORKER, bump, NULL, c);
    for (int i = 0; i < 50; ++i)  jobs_submit(JOB_LANE_DECODE, bump, NULL, c);
    for (int i = 0; i < 20; ++i)  jobs_submit(JOB_LANE_IO, bump, NULL, c);
    jobs_wait(c);
    CHECK(g_counter_hits.load() == 270, "every lane ran its jobs: %d of 270", g_counter_hits.load());

    job_stats_t st;
    jobs_stats(&st);
    CHECK(st.lane[JOB_LANE_WORKER].executed >= 200, "worker lane executed count: %lld", st.lane[JOB_LANE_WORKER].executed);
    CHECK(st.lane[JOB_LANE_DECODE].executed >= 50, "decode lane executed count: %lld", st.lane[JOB_LANE_DECODE].executed);
    CHECK(st.lane[JOB_LANE_IO].executed == 20, "io lane executed count: %lld", st.lane[JOB_LANE_IO].executed);
    CHECK(st.lane[JOB_LANE_WORKER].queued == 0, "worker lane drained");
    jobs_shutdown();
}

static void test_parallel_for(void) {
    jobs_init(4);
    std::vector<int> hits(100000, 0);
    std::atomic<int> bad(0);
    range_check_t rc = { &hits, &bad };
    jobs_parallel_for(JOB_LANE_WORKER, range_mark, &rc, 100000, 256);
    int once = 0;
    for (size_t i = 0; i < hits.size(); ++i) once += (hits[i] == 1);
    CHECK(once == 100000, "parallel_for covers every index exactly once: %d of 100000", once);
    CHECK(bad.load() == 0, "slot index stayed inside [0, slot_count): %d bad", bad.load());

    // count not a multiple of grain, and grain larger than count
    hits.assign(1001, 0);
    jobs_parallel_for(JOB_LANE_WORKER, range_mark, &rc, 1001, 100);
    once = 0;
    for (int i = 0; i < 1001; ++i) once += (hits[i] == 1);
    CHECK(once == 1001, "ragged last chunk: %d of 1001", once);
    hits.assign(7, 0);
    jobs_parallel_for(JOB_LANE_WORKER, range_mark, &rc, 7, 64);
    once = 0;
    for (int i = 0; i < 7; ++i) once += (hits[i] == 1);
    CHECK(once == 7, "grain larger than count: %d of 7", once);
    jobs_shutdown();
}

static void test_chunk_determinism(void) {
    const int N = 50000;
    std::vector<double> v((size_t)N);
    for (int i = 0; i < N; ++i) v[(size_t)i] = 1.0 / (i + 1) + (i % 7) * 1e-9;

    jobs_init(0);
    double seq = reduce_run(v.data(), N, 128);
    jobs_shutdown();
    jobs_init(1);
    double one = reduce_run(v.data(), N, 128);
    jobs_shutdown();
    jobs_init(8);
    double many = reduce_run(v.data(), N, 128);
    jobs_shutdown();

    // identical bit for bit: the chunks, and therefore the order of the additions, do not depend
    // on the worker count
    CHECK(seq == one && one == many, "per-chunk reduction is worker-count independent: %.17g / %.17g / %.17g", seq, one, many);
}

static std::atomic<int> g_nested_done(0);
static void nested_body(void *arg, int begin, int end, int slot) { (void)arg; (void)slot; g_nested_done.fetch_add(end - begin); }
static void nested_outer(void *arg) {
    (void)arg;
    jobs_parallel_for(JOB_LANE_WORKER, nested_body, NULL, 500, 32);   // waits inside a job: must not deadlock
}

static void test_nested(void) {
    jobs_init(4);
    g_nested_done.store(0);
    job_counter_t c = jobs_counter();
    for (int i = 0; i < 8; ++i) jobs_submit(JOB_LANE_WORKER, nested_outer, NULL, c);
    jobs_wait(c);
    CHECK(g_nested_done.load() == 8 * 500, "nested parallel_for inside a job: %d of %d", g_nested_done.load(), 8 * 500);
    jobs_shutdown();
}

static std::atomic<int> g_main_runs(0);
static std::atomic<int> g_main_wrong_thread(0);
static int g_main_submit_n = 12;
static void main_job(void *arg) {
    (void)arg;
    if (std::this_thread::get_id() != main_id) g_main_wrong_thread.fetch_add(1);
    g_main_runs.fetch_add(1);
}
static void submit_main_jobs(void *arg) {
    int n = *(int *)arg;
    for (int i = 0; i < n; ++i) jobs_submit(JOB_LANE_MAIN, main_job, NULL, JOB_NO_COUNTER);
}

static void test_main_lane(void) {
    jobs_init(4);
    main_id = std::this_thread::get_id();

    // queued from the render thread: nothing runs them until it asks
    g_main_runs.store(0); g_main_wrong_thread.store(0);
    for (int i = 0; i < 12; ++i) jobs_submit(JOB_LANE_MAIN, main_job, NULL, JOB_NO_COUNTER);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(g_main_runs.load() == 0, "MAIN jobs wait for a drain, %d ran early", g_main_runs.load());
    int ran = jobs_main_drain(0);
    CHECK(ran == 12, "drain runs every queued MAIN job: %d of 12", ran);
    CHECK(g_main_runs.load() == 12, "MAIN jobs executed: %d of 12", g_main_runs.load());

    // queued from a worker: run either by the drain or by the render thread while it waits, but
    // in both cases on the render thread and nowhere else
    g_main_runs.store(0);
    job_counter_t c = jobs_counter();
    jobs_submit(JOB_LANE_WORKER, submit_main_jobs, &g_main_submit_n, c);
    jobs_wait(c);
    jobs_main_drain(0);
    CHECK(g_main_runs.load() == 12, "MAIN jobs queued by a worker ran: %d of 12", g_main_runs.load());
    CHECK(g_main_wrong_thread.load() == 0, "MAIN jobs only ever ran on the render thread");
    jobs_shutdown();
}

static void slow_main_job(void *arg) { (void)arg; std::this_thread::sleep_for(std::chrono::milliseconds(2)); g_main_runs.fetch_add(1); }

static void test_main_budget(void) {
    jobs_init(2);
    g_main_runs.store(0);
    for (int i = 0; i < 20; ++i) jobs_submit(JOB_LANE_MAIN, slow_main_job, NULL, JOB_NO_COUNTER);
    int first = jobs_main_drain(5.0);               // 20 x 2 ms of work, 5 ms of budget
    CHECK(first > 0 && first < 20, "the upload budget stops the drain early: %d of 20 ran", first);
    int total = first;
    for (int i = 0; i < 40 && total < 20; ++i) total += jobs_main_drain(5.0);
    CHECK(total == 20, "the rest drain over later frames: %d of 20", total);
    CHECK(jobs_main_drain(0) == 0, "queue empty at the end");
    jobs_shutdown();
}

static void test_backpressure(void) {
    jobs_init(2);
    g_counter_hits.store(0);
    const int N = mare::core::WorkerPool::QUEUE_CAP * 3;                // more than one lane can hold at once
    job_counter_t c = jobs_counter();
    for (int i = 0; i < N; ++i) jobs_submit(JOB_LANE_WORKER, bump, NULL, c);
    jobs_wait(c);
    CHECK(g_counter_hits.load() == N, "a full lane runs jobs inline rather than dropping them: %d of %d", g_counter_hits.load(), N);
    jobs_shutdown();
}

static void test_counters(void) {
    jobs_init(2);
    job_counter_t c = jobs_counter();
    CHECK(jobs_done(JOB_NO_COUNTER), "the null counter counts as done");
    CHECK(jobs_done(c), "a fresh counter is done");
    jobs_release(c);
    CHECK(jobs_done(c), "a released handle is stale, not pending");
    g_counter_hits.store(0);
    jobs_submit(JOB_LANE_WORKER, bump, NULL, c);    // stale handle: the job still runs
    job_counter_t c2 = jobs_counter();
    jobs_wait(c2);
    for (int i = 0; i < 100 && g_counter_hits.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(g_counter_hits.load() == 1, "a job with a stale counter still runs");

    // the slab is finite: allocate it dry and check the caller gets a clean refusal
    std::vector<job_counter_t> all;
    for (;;) { job_counter_t h = jobs_counter(); if (!h) break; all.push_back(h); }
    CHECK(all.size() == (size_t)mare::core::WorkerPool::MAX_COUNTERS, "counter slab size: %d", (int)all.size());
    for (size_t i = 0; i < all.size(); ++i) jobs_release(all[i]);
    CHECK(jobs_counter() != JOB_NO_COUNTER, "counters are reusable after release");
    jobs_shutdown();
}

static void test_shutdown_drains(void) {
    jobs_init(4);
    g_counter_hits.store(0);
    for (int i = 0; i < 500; ++i) jobs_submit(JOB_LANE_DECODE, bump, NULL, JOB_NO_COUNTER);
    jobs_shutdown();                                 // must not lose the queued work
    CHECK(g_counter_hits.load() == 500, "shutdown completes queued jobs: %d of 500", g_counter_hits.load());

    // after shutdown the API is synchronous again rather than silently dropping work
    g_counter_hits.store(0);
    jobs_submit(JOB_LANE_WORKER, bump, NULL, JOB_NO_COUNTER);
    CHECK(g_counter_hits.load() == 1, "submits after shutdown run inline");
}

int main(void) {
    main_id = std::this_thread::get_id();
    test_synchronous();
    test_lanes();
    test_parallel_for();
    test_chunk_determinism();
    test_nested();
    test_main_lane();
    test_main_budget();
    test_backpressure();
    test_counters();
    test_shutdown_drains();

    { int n = jobs_init(-1);                         // auto sizing (MARE_JOBS may pin it)
      printf("auto worker count: %d (hardware_concurrency %u)\n", n, std::thread::hardware_concurrency());
      CHECK(n >= 0 && n <= mare::core::WorkerPool::MAX_WORKERS, "auto worker count is sane: %d", n);
      jobs_shutdown(); }

    if (failures) printf("jobs_test: %d FAILURES\n", failures);
    else          printf("jobs_test: ok\n");
    return failures ? 1 : 0;
}
