#pragma once

#include <cstdint>

namespace game::event { class EventDispatcher; }
namespace game::world { class World; }

namespace game::sim {

// Context passed into each command on execute. Lives only for the duration
// of a single execute() call; safe to hold references.
struct CommandContext {
    game::world::World& world;
    game::event::EventDispatcher& events;
    uint64_t tick;  // value of world.tick() at the start of command processing
};

// Base class for commands pushed through the sim command queue. Commands are
// constructed on IO threads (from packet handlers) and executed on the sim
// thread during the queue-drain phase of each tick.
class Command {
public:
    virtual ~Command() = default;
    virtual void execute(CommandContext& ctx) = 0;
    // For logs / diagnostics — no runtime dispatch depends on this.
    virtual const char* name() const = 0;
};

} // namespace game::sim
