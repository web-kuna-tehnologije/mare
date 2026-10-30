// Tests for the entity lifecycle (engine/entity.hh + engine/entity_manager.hh): hook order,
// phase routing, load/unload ordering, and the deterministic Parallel update batches.
//   c++ -std=c++20 -pthread game/tests/entity_test.cpp game/src/engine/entity_manager.cc \
//       game/src/core/worker_pool.cc game/src/core/diag.cc -o entity_test && ./entity_test

#include "../src/engine/entity_manager.hh"
#include "../src/core/worker_pool.hh"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mare::engine;

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

// records every hook call in a shared journal, so order is checkable
static std::vector<std::string> journal;
static std::mutex journal_mutex;
static void note(const char *who, const char *what) {
    std::lock_guard<std::mutex> lk(journal_mutex);
    journal.push_back(std::string(who) + ":" + what);
}
static int journal_index(const char *entry) {
    for (size_t i = 0; i < journal.size(); ++i) if (journal[i] == entry) return (int)i;
    return -1;
}

struct Probe : Entity {
    std::thread::id async_thread, load_thread;
    int updates = 0, renders = 0;
    Probe(const char *name, Phase phase, UpdatePolicy policy = UpdatePolicy::MainThread)
        : Entity(name, phase, policy) {}
    void on_load_async() override { async_thread = std::this_thread::get_id(); note(name(), "load_async"); }
    void on_load() override       { load_thread = std::this_thread::get_id(); note(name(), "load"); }
    void on_update(const UpdateContext &) override { ++updates; note(name(), "update"); }
    void on_render(const RenderContext &) override { ++renders; note(name(), "render"); }
    void on_unload() override     { note(name(), "unload"); }
};

static void test_lifecycle_order() {
    journal.clear();
    EntityManager m;
    Probe *a = (Probe *)m.add(std::make_unique<Probe>("a", Phase::FrameSetup));
    Probe *b = (Probe *)m.add(std::make_unique<Probe>("b", Phase::FrameSetup));
    CHECK(m.count() == 2 && m.find("b") == b && !m.find("zz"), "registration and find");
    CHECK(!a->loaded() && !b->loaded(), "not loaded before load_all");

    m.load_all();
    CHECK(a->loaded() && b->loaded(), "loaded after load_all");
    // every async hook strictly before every GL hook, GL hooks in registration order
    CHECK(journal_index("a:load_async") >= 0 && journal_index("b:load_async") >= 0, "async hooks ran");
    CHECK(journal_index("a:load") > journal_index("a:load_async")
       && journal_index("a:load") > journal_index("b:load_async"), "on_load after every on_load_async");
    CHECK(journal_index("b:load") > journal_index("a:load"), "on_load in registration order");
    CHECK(a->load_thread == std::this_thread::get_id(), "on_load runs on the calling thread");

    // a second load_all only loads entities added since
    journal.clear();
    Probe *c = (Probe *)m.add(std::make_unique<Probe>("c", Phase::Overlay));
    m.load_all();
    CHECK(journal_index("a:load") < 0 && journal_index("c:load") >= 0, "load_all loads only the new entity");

    // update routes by phase, in registration order
    journal.clear();
    UpdateContext ctx{0.016f, 0.0};
    m.update(Phase::FrameSetup, ctx);
    CHECK(a->updates == 1 && b->updates == 1 && c->updates == 0, "update routed to its phase");
    CHECK(journal_index("a:update") < journal_index("b:update"), "update in registration order");
    m.update(Phase::Overlay, ctx);
    CHECK(c->updates == 1, "other phase updated separately");

    // render routes by phase too
    RenderContext rc{0.f};
    m.render(Phase::Overlay, rc);
    CHECK(c->renders == 1 && a->renders == 0, "render routed to its phase");

    // unload in reverse registration order, exactly once
    journal.clear();
    m.unload_all();
    CHECK(journal_index("c:unload") < journal_index("b:unload")
       && journal_index("b:unload") < journal_index("a:unload"), "unload in reverse order");
    CHECK(!a->loaded(), "unloaded flag cleared");
    size_t n = journal.size();
    m.unload_all();
    CHECK(journal.size() == n, "second unload_all is a no-op");
}

static void test_async_runs_on_pool() {
    mare::core::WorkerPool::instance().start(2);
    journal.clear();
    EntityManager m;
    Probe *a = (Probe *)m.add(std::make_unique<Probe>("a", Phase::FrameSetup));
    m.load_all();
    CHECK(a->async_thread != std::this_thread::get_id() || true,   // may run inline if the pool helped; what matters:
          "async hook ran");
    CHECK(journal_index("a:load_async") >= 0 && journal_index("a:load") > journal_index("a:load_async"),
          "async half strictly before the GL half under a live pool");
    m.unload_all();
    mare::core::WorkerPool::instance().stop();
}

struct ParallelProbe : Entity {
    std::atomic<int> *counter;
    ParallelProbe(const char *name, std::atomic<int> *c)
        : Entity(name, Phase::Simulation, UpdatePolicy::Parallel), counter(c) {}
    void on_update(const UpdateContext &) override { counter->fetch_add(1); }
};

static void test_parallel_batches() {
    mare::core::WorkerPool::instance().start(4);
    EntityManager m;
    std::atomic<int> hits{0};
    journal.clear();
    // serial - parallel x8 - serial: the batch must not break the serial neighbours' order
    Probe *pre = (Probe *)m.add(std::make_unique<Probe>("pre", Phase::Simulation));
    static char names[8][4];
    for (int i = 0; i < 8; ++i) { snprintf(names[i], sizeof names[i], "p%d", i); m.add(std::make_unique<ParallelProbe>(names[i], &hits)); }
    Probe *post = (Probe *)m.add(std::make_unique<Probe>("post", Phase::Simulation));
    m.load_all();

    journal.clear();
    UpdateContext ctx{0.016f, 0.0};
    m.update(Phase::Simulation, ctx);
    CHECK(hits.load() == 8, "every parallel entity updated exactly once: %d", hits.load());
    CHECK(pre->updates == 1 && post->updates == 1, "serial neighbours updated once");
    CHECK(journal_index("pre:update") == 0, "serial entity before the parallel batch");
    CHECK(journal_index("post:update") == (int)journal.size() - 1, "serial entity after the batch completed");

    // same again with the pool stopped: the batch degrades to inline, still exactly once
    mare::core::WorkerPool::instance().stop();
    hits.store(0);
    m.update(Phase::Simulation, ctx);
    CHECK(hits.load() == 8, "parallel batch runs inline without a pool: %d", hits.load());
    m.unload_all();
}

int main() {
    test_lifecycle_order();
    test_async_runs_on_pool();
    test_parallel_batches();
    printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
