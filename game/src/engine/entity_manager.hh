// mare::engine::EntityManager - owns the entities and drives their lifecycle (entity.hh).
//
// Loading: on_load_async for every added entity is submitted to the worker pool's DECODE
// lane and waited on (helping while it waits), then on_load runs serially on the calling
// (main/GL) thread in registration order. Entities added after load_all() are loaded by the
// next load_all() call. Unloading runs on_unload in reverse registration order.
//
// The C ABI at the bottom (declared in entities.h) is what the unported main loop calls.

#ifndef MARE_ENGINE_ENTITY_MANAGER_HH
#define MARE_ENGINE_ENTITY_MANAGER_HH

#include "entity.hh"

#include <memory>
#include <vector>

namespace mare::engine {

class EntityManager {
public:
    static EntityManager &instance();

    EntityManager() = default;
    ~EntityManager() { unload_all(); }
    EntityManager(const EntityManager &) = delete;
    EntityManager &operator=(const EntityManager &) = delete;

    // Registration order is update/render order within a phase. Returns the entity for
    // convenience (the manager keeps ownership).
    Entity *add(std::unique_ptr<Entity> e);
    Entity *find(const char *name) const;
    int     count() const { return (int)entities_.size(); }

    void load_all();                                    // async hooks on the pool, then on_load here
    void unload_all();                                  // reverse order; loaded entities only
    void update(Phase phase, const UpdateContext &ctx); // serial, with Parallel runs batched on the pool
    void render(Phase phase, const RenderContext &ctx);

private:
    std::vector<std::unique_ptr<Entity>> entities_;
};

} // namespace mare::engine

#endif
