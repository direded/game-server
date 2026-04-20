#include "sim/world.h"

namespace game::sim {

void World::advance() {
    ++tick;
}

} // namespace game::sim
