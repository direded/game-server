#pragma once

namespace game::event { class EventDispatcher; }
namespace game::session { class SessionManager; }
namespace game::world { class World; }

namespace game::sim {

// Per-tick driver that advances every character's `current_action` by one
// elapsed tick and resolves any that finish on this tick. Lives as part of
// the SimLoop pipeline (between command drain and world.advance()).
//
// Sim-thread only — no synchronization. Holds a SessionManager& because
// resolving a Travel needs to (a) update the cached session.location_id so
// IO-thread chat keeps routing Local correctly, and (b) look up the actor's
// connection to send the private ActionCompleted.
class ActionResolver {
public:
    explicit ActionResolver(session::SessionManager& sessions)
        : sessions_(sessions) {}

    void tick(world::World& world, event::EventDispatcher& events);

private:
    session::SessionManager& sessions_;
};

} // namespace game::sim
