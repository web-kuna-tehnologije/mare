// mare::game::EntityRegistry - which entities the game puts into the engine, and in what
// order. Registration order is update/render order within a phase (engine/entity_manager.hh),
// so modules keep the exact slot their legacy call sites had as they are ported; the class
// exists so that order is stated in one named place rather than in a loose function.

#include "../engine/entity_manager.hh"
#include "../engine/entities.h"

#include <memory>

namespace mare::game {

std::unique_ptr<engine::Entity> make_weather();

class EntityRegistry {
public:
    static EntityRegistry &instance() { static EntityRegistry r; return r; }

    // Called once, before entities_load(). Add new entities here, in the order they should
    // update and render.
    void register_all() {
        if (registered_) return;
        engine::EntityManager &m = engine::EntityManager::instance();
        m.add(make_weather());
        registered_ = true;
    }
    int registered() const { return registered_ ? 1 : 0; }

private:
    bool registered_ = false;
};

} // namespace mare::game

extern "C" void entities_startup(void) { mare::game::EntityRegistry::instance().register_all(); }
