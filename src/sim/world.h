#pragma once

#include <cstdint>

namespace game::sim {

// Placeholder world. Step-006 only needs a monotonic tick counter so
// CommandContext is meaningful; step-007 replaces the guts with the real
// Location / Character / Action model.
struct World {
    uint64_t tick = 0;

    // Sim-thread only. Advances the tick counter; no world state otherwise.
    void advance();
};

} // namespace game::sim
