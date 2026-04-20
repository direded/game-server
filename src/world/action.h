#pragma once

#include "world/ids.h"

#include <cstdint>
#include <variant>

namespace game::world {

// Discriminator for what kind of action a character is performing. Future
// gameplay actions extend this enum (Gather, Craft, UseObject, ...). The
// variant in `Action::params` carries the per-kind payload.
enum class ActionKind : uint8_t {
    Travel = 0,
};

struct TravelParams {
    LocationId to = 0;
};

// One in-flight action. Lives on `Character::current_action` while the action
// is running; reset to std::nullopt when it completes or is cancelled. Owned
// by the world (sim-thread only — no synchronization).
struct Action {
    ActionKind kind = ActionKind::Travel;
    uint64_t started_tick = 0;
    uint16_t duration_ticks = 0;
    uint16_t elapsed_ticks = 0;
    std::variant<TravelParams> params;
};

} // namespace game::world
