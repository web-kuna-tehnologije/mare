// mare::engine::EntityManager implementation (see entity_manager.hh).

#include "entity_manager.hh"
#include "entities.h"

#include "../core/diag.hh"
#include "../core/worker_pool.hh"

#include <cstring>

namespace mare::engine {

EntityManager &EntityManager::instance() {
    static EntityManager m;
    return m;
}

Entity *EntityManager::add(std::unique_ptr<Entity> e) {
    entities_.push_back(std::move(e));
    return entities_.back().get();
}

Entity *EntityManager::find(const char *name) const {
    for (const auto &e : entities_)
        if (!strcmp(e->name(), name)) return e.get();
    return nullptr;
}

void EntityManager::load_all() {
    using core::WorkerPool;
    WorkerPool &pool = WorkerPool::instance();

    // Phase 1: the async halves, fanned out on the DECODE lane (same priority class as other
    // loading work, so a frame-critical parallel_for never queues behind them). The job
    // argument is the entity itself, owned by entities_ - it outlives the job.
    struct async_call_t { static void run(void *arg) { static_cast<Entity *>(arg)->on_load_async(); } };
    job_counter_t c = pool.make_counter();
    int submitted = 0;
    for (auto &e : entities_)
        if (!e->loaded_) { pool.submit(JOB_LANE_DECODE, async_call_t::run, e.get(), c); ++submitted; }
    if (submitted) pool.wait(c); else pool.release(c);

    // Phase 2: the GL halves, serially on this (the main) thread, in registration order.
    for (auto &e : entities_)
        if (!e->loaded_) { e->on_load(); e->loaded_ = true; }
}

void EntityManager::unload_all() {
    for (auto it = entities_.rbegin(); it != entities_.rend(); ++it)
        if ((*it)->loaded_) { (*it)->on_unload(); (*it)->loaded_ = false; }
}

void EntityManager::update(Phase phase, const UpdateContext &ctx) {
    using core::WorkerPool;
    // Walk the registration order; a run of adjacent Parallel entities in this phase is
    // dispatched as one deterministic parallel_for (entity.hh spells out when that is legal).
    const int n = (int)entities_.size();
    for (int i = 0; i < n; ++i) {
        Entity *e = entities_[i].get();
        if (e->phase() != phase || !e->loaded_) continue;
        if (e->update_policy() != UpdatePolicy::Parallel) { e->on_update(ctx); continue; }
        int j = i;  // the contiguous Parallel run [i, j]
        while (j + 1 < n && entities_[j + 1]->phase() == phase
               && entities_[j + 1]->loaded_ && entities_[j + 1]->update_policy() == UpdatePolicy::Parallel) ++j;
        if (j == i) { e->on_update(ctx); continue; }
        // collect the run's raw pointers so the chunks index plain memory
        static thread_local std::vector<Entity *> batch;
        batch.clear();
        for (int k = i; k <= j; ++k) batch.push_back(entities_[k].get());
        struct batch_call_t { Entity *const *ents; const UpdateContext *ctx; };
        batch_call_t bc = { batch.data(), &ctx };
        WorkerPool::instance().parallel_for(JOB_LANE_WORKER,
            [](void *arg, int begin, int end, int) {
                batch_call_t *b = static_cast<batch_call_t *>(arg);
                for (int k = begin; k < end; ++k) b->ents[k]->on_update(*b->ctx);
            }, &bc, (int)batch.size(), 1);
        i = j;
    }
}

void EntityManager::render(Phase phase, const RenderContext &ctx) {
    for (auto &e : entities_)
        if (e->phase() == phase && e->loaded_) e->on_render(ctx);
}

} // namespace mare::engine

// ---------------------------------------------------------------------------
// C ABI (entities.h): the unported main loop drives the manager through these

using mare::engine::EntityManager;
using mare::engine::Phase;

static Phase phase_from(int p) {
    return (p >= 0 && p < (int)Phase::Count) ? (Phase)p : Phase::FrameSetup;
}

extern "C" {

void entities_load(void) { EntityManager::instance().load_all(); }
void entities_unload(void) { EntityManager::instance().unload_all(); }
int  entities_count(void) { return EntityManager::instance().count(); }

void entities_update(int phase, float dt) {
    mare::engine::UpdateContext ctx; ctx.dt = dt; ctx.now = mare::core::monotonic_now();
    EntityManager::instance().update(phase_from(phase), ctx);
}

void entities_render(int phase, float mapness) {
    mare::engine::RenderContext ctx; ctx.mapness = mapness;
    EntityManager::instance().render(phase_from(phase), ctx);
}

} // extern "C"
