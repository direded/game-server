#pragma once

#include "session/session.h"
#include "world/action.h"
#include "world/ids.h"

#include <optional>
#include <string>

namespace game::world {

using session::AccountId;

// Authoritative in-RAM representation of a character. Persisted in Postgres
// via CharacterStore for durability of (id, account_id, name, location_id);
// `current_action` is RAM-only (cancelled on disconnect, never crash-recovered).
//
// Sim-thread only — no synchronization. CharacterStore reads/writes happen
// off the sim thread but only mutate persisted columns, never current_action.
struct Character {
    CharacterId id = 0;
    AccountId account_id = 0;
    std::string name;
    LocationId location_id = 0;
    std::optional<Action> current_action;
};

} // namespace game::world
