#pragma once

#include "session/session.h"
#include "world/character.h"
#include "world/ids.h"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game::db { class Connection; }

namespace game::world {

using session::AccountId;

// Postgres-backed CRUD for the `characters` table. Like PostgresAuthStore,
// serializes all libpq calls behind an internal mutex (PGconn isn't safe for
// concurrent use) — fine at this scale; revisit when characters become a hot
// path. The Connection reference is non-owning; main.cpp owns it.
//
// `current_action` is RAM-only — never persisted by this store.
class CharacterStore {
public:
    explicit CharacterStore(db::Connection& conn);

    // Insert a new character. Throws on unique-violation if `name` is taken
    // (caller is expected to map this to NameTaken on the wire).
    Character create(AccountId account_id,
                     std::string_view name,
                     LocationId spawn_location_id);

    // Snapshot of every character in the table. Used at server startup to
    // populate the in-RAM World map. Within one server lifetime location
    // changes live in RAM only; persisting them through Travel is future
    // work (see "Snapshots to Postgres" in step-007 scope notes).
    std::vector<Character> load_all();

    // Look up by id. Used by tests that bypass the World cache.
    std::optional<Character> find_by_id(CharacterId id);

    // Used by tests that want a quick "did the row land" check.
    std::vector<Character> list_by_account(AccountId account_id);

private:
    db::Connection& conn_;
    mutable std::mutex mu_;
};

// Server-side validation for character names. Same shape as the spec rule:
//   3–24 chars, [A-Za-z][A-Za-z0-9_ -]{2,23}
bool is_valid_character_name(std::string_view name);

} // namespace game::world
