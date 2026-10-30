// C ABI over mare::engine::EntityManager (entity_manager.hh) for the unported main loop.
// Phases mirror mare::engine::Phase; the values must stay in step.

#ifndef MARE_ENGINE_ENTITIES_H
#define MARE_ENGINE_ENTITIES_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ENT_PHASE_INPUT = 0,
    ENT_PHASE_SIMULATION,
    ENT_PHASE_FRAME,        // after the sim, before any pass (sun, waves, weather, sky)
    ENT_PHASE_SCENE,
    ENT_PHASE_OVERLAY,      // over the finished, tonemapped frame
    ENT_PHASE_COUNT
};

void entities_startup(void);              // construct and register the built-in C++ entities (game side)
void entities_load(void);                 // async loads on the pool, GL loads on this thread
void entities_update(int phase, float dt);
void entities_render(int phase, float mapness);
void entities_unload(void);               // reverse registration order
int  entities_count(void);

#ifdef __cplusplus
}
#endif

#endif
