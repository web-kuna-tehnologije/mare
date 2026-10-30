// Registration of the game's C++ entities (mare::game) with the engine's EntityManager.
// entities_startup() is the C entry the main loop calls once, before entities_load();
// registration order is update/render order within a phase (entity_manager.hh), so modules
// keep the exact slot their legacy call sites had as they are ported.

#include "../engine/entity_manager.hh"
#include "../engine/entities.h"

#include <memory>

namespace mare::game {
std::unique_ptr<engine::Entity> make_weather();
} // namespace mare::game

extern "C" void entities_startup(void) {
    using mare::engine::EntityManager;
    EntityManager &m = EntityManager::instance();
    m.add(mare::game::make_weather());
}
