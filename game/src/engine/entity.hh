// mare::engine::Entity - the lifecycle every ported game object plugs into.
//
// Hooks, in the order the EntityManager drives them:
//
//   on_load_async()   worker pool (DECODE lane): parsing, decoding, mesh building - no GL
//   on_load()         main/GL thread, after on_load_async finished: GPU uploads, pipelines
//   on_update(ctx)    once per frame in the entity's phase; registration order within a phase
//   on_render(ctx)    main/GL thread only, in the entity's phase of the render
//   on_unload()       main thread, reverse registration order: release GPU and CPU resources
//
// Update threading: UpdatePolicy::MainThread (the default) entities run serially in
// registration order. UpdatePolicy::Parallel entities that are *adjacent in registration
// order within one phase* are dispatched together through core::WorkerPool::parallel_for -
// an entity may only declare Parallel if its on_update touches no state another entity
// reads or writes in the same phase. Chunking is deterministic (see worker_pool.hh), so a
// Parallel group never introduces run-to-run divergence by itself.

#ifndef MARE_ENGINE_ENTITY_HH
#define MARE_ENGINE_ENTITY_HH

namespace mare::engine {

// The frame phases the main loop drives, in order. These mirror the fixed pass structure in
// mare.c; ported entities slot into the phase their legacy call site occupied.
enum class Phase {
    Input,          // before the simulation: input, camera
    Simulation,     // the sim tick window
    FrameSetup,     // after the sim, before any pass: sun, waves, weather, sky state
    Scene,          // scene build / submit (opaque passes)
    Overlay,        // over the finished, tonemapped frame: rain, transitions
    Count
};

enum class UpdatePolicy { MainThread, Parallel };

struct UpdateContext {
    float  dt;      // wall-clock seconds since the last frame (clamped as in the main loop)
    double now;     // monotonic seconds (diag_now clock)
};

struct RenderContext {
    float mapness;  // 0 = 3D view, 1 = map view (what most overlay passes gate on)
};

class Entity {
public:
    Entity(const char *name, Phase phase, UpdatePolicy policy = UpdatePolicy::MainThread)
        : name_(name), phase_(phase), policy_(policy) {}
    virtual ~Entity() = default;
    Entity(const Entity &) = delete;
    Entity &operator=(const Entity &) = delete;

    const char  *name() const { return name_; }
    Phase        phase() const { return phase_; }
    UpdatePolicy update_policy() const { return policy_; }
    bool         loaded() const { return loaded_; }

    // lifecycle hooks (see the header comment for who calls what, where)
    virtual void on_load_async() {}
    virtual void on_load() {}
    virtual void on_update(const UpdateContext &) {}
    virtual void on_render(const RenderContext &) {}
    virtual void on_unload() {}

private:
    friend class EntityManager;     // the only writer of loaded_
    const char  *name_;
    Phase        phase_;
    UpdatePolicy policy_;
    bool         loaded_ = false;
};

} // namespace mare::engine

#endif
