#pragma once

#include <cstdint>

namespace game::world {

// Type aliases for the small set of opaque ids the world model passes around.
// Lifted into their own header so location.h and character.h can include it
// without forming a cycle (Location holds a set of CharacterId; Character
// holds a LocationId).
using LocationId = uint32_t;
using CharacterId = uint64_t;

} // namespace game::world
