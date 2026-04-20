#pragma once

#include "world/ids.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace game::world {

// Directed edge in the location graph. The loader validates that every link
// has a matching reverse (graph is undirected at runtime), so a Travel from
// A → B implies B → A exists too.
struct LocationLink {
    LocationId to = 0;
    uint16_t travel_ticks = 0;
};

// One node in the location graph. The set of `characters` currently here is
// mutated by Travel completion and SelectCharacter / DeselectCharacter; it is
// the source of truth used by the event dispatcher to route Local-scope
// events.
struct Location {
    LocationId id = 0;
    std::string name;
    std::string description;
    std::vector<LocationLink> links;
    std::unordered_set<CharacterId> characters;
};

} // namespace game::world
