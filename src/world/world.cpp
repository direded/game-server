#include "world/world.h"

namespace game::world {

void World::advance() {
    ++tick_;
}

void World::install_locations(LocationGraph graph) {
    spawn_id_ = graph.spawn_id;
    locations_ = std::move(graph.locations);
}

Location* World::location(LocationId id) {
    auto it = locations_.find(id);
    return it == locations_.end() ? nullptr : &it->second;
}

const Location* World::location(LocationId id) const {
    auto it = locations_.find(id);
    return it == locations_.end() ? nullptr : &it->second;
}

void World::install_characters(std::vector<Character> chars) {
    for (auto& c : chars) {
        const auto id = c.id;
        characters_.emplace(id, std::move(c));
    }
}

void World::add_character(Character c) {
    const auto id = c.id;
    characters_.emplace(id, std::move(c));
}

Character* World::character(CharacterId id) {
    auto it = characters_.find(id);
    return it == characters_.end() ? nullptr : &it->second;
}

const Character* World::character(CharacterId id) const {
    auto it = characters_.find(id);
    return it == characters_.end() ? nullptr : &it->second;
}

bool World::move_character(CharacterId char_id, LocationId to) {
    auto* ch = character(char_id);
    if (!ch) return false;
    auto* from_loc = location(ch->location_id);
    auto* to_loc = location(to);
    if (!from_loc || !to_loc) return false;
    from_loc->characters.erase(char_id);
    to_loc->characters.insert(char_id);
    ch->location_id = to;
    return true;
}

void World::place_character_in_location(CharacterId char_id, LocationId at) {
    if (auto* loc = location(at)) {
        loc->characters.insert(char_id);
    }
}

void World::remove_character_from_location(CharacterId char_id, LocationId at) {
    if (auto* loc = location(at)) {
        loc->characters.erase(char_id);
    }
}

} // namespace game::world
