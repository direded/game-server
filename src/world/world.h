#pragma once

#include "world/character.h"
#include "world/ids.h"
#include "world/location.h"
#include "world/locations_loader.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game::world {

// Authoritative in-RAM model: tick counter + location graph + character map.
//
// Sim-thread only. The IO thread must NOT mutate the world directly; it must
// post a Command to the sim queue. Read access from non-sim threads is also
// disallowed — anything an IO-thread handler needs (e.g. chat needs to know
// the speaker's location) is mirrored onto Session by the sim thread when it
// changes, so IO threads only ever read SessionManager.
class World {
public:
    World() = default;

    // Step the tick counter. Called by SimLoop once per tick.
    void advance();
    uint64_t tick() const { return tick_; }

    // ── Location graph (loaded once at startup; immutable after) ──────────
    void install_locations(LocationGraph graph);
    LocationId spawn_location_id() const { return spawn_id_; }
    Location* location(LocationId id);
    const Location* location(LocationId id) const;
    const std::unordered_map<LocationId, Location>& locations() const { return locations_; }

    // ── Characters ────────────────────────────────────────────────────────
    // Bulk install at startup from CharacterStore::load_all().
    void install_characters(std::vector<Character> chars);
    // Insert a freshly-created character returned by CharacterStore::create.
    void add_character(Character c);
    Character* character(CharacterId id);
    const Character* character(CharacterId id) const;
    const std::unordered_map<CharacterId, Character>& characters() const { return characters_; }

    // Helper: move a character between location sets and update its
    // `location_id`. Both endpoints must already be installed. Returns false
    // if either id is unknown — caller should treat that as a bug (fail-loud
    // is fine for the sim-thread path).
    bool move_character(CharacterId char_id, LocationId to);

    // Add/remove a character from a location's member set without changing
    // the character's home location_id. Used by SelectCharacter (add to
    // current location's set) and DeselectCharacter (remove).
    void place_character_in_location(CharacterId char_id, LocationId at);
    void remove_character_from_location(CharacterId char_id, LocationId at);

private:
    uint64_t tick_ = 0;
    LocationId spawn_id_ = 0;
    std::unordered_map<LocationId, Location> locations_;
    std::unordered_map<CharacterId, Character> characters_;
};

} // namespace game::world
