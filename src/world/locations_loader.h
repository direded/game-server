#pragma once

#include "world/ids.h"
#include "world/location.h"

#include <stdexcept>
#include <string>
#include <unordered_map>

namespace game::world {

struct LocationGraph {
    LocationId spawn_id = 0;
    std::unordered_map<LocationId, Location> locations;
};

struct LocationLoadError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parse a YAML file describing the location graph. Validates:
//   - every location has a unique id
//   - spawn_location_id refers to an existing location
//   - every link's `to` refers to an existing location
//   - every link is symmetric (if A → B exists, B → A exists)
//   - travel_ticks > 0
//
// Throws LocationLoadError on any validation failure. The graph is meant to
// be loaded once at startup; a malformed YAML aborts the boot.
LocationGraph load_locations(const std::string& path);

// Same as load_locations() but reads from an in-memory YAML string. Used by
// tests so they don't need temp files.
LocationGraph load_locations_from_string(const std::string& yaml);

} // namespace game::world
