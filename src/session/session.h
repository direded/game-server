#pragma once

#include "net/transport.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace game::session {

using AccountId = uint64_t;

enum class AuthState : uint8_t {
    Unauthenticated,
    Authenticated,
};

struct Session {
    net::ConnId conn_id = 0;
    std::string remote_addr;
    std::chrono::steady_clock::time_point opened_at{};
    AuthState state = AuthState::Unauthenticated;
    std::optional<AccountId> account_id;
    // Display name resolved once at auth time (chat fanout reads this to
    // avoid a DB hit per ChatSay). Empty for Unauthenticated sessions.
    std::string username;

    // Selected character. When set, the session is in the "CharacterSelected"
    // sub-state and may issue gameplay packets (StartAction, CancelAction,
    // local chat). location_id mirrors the character's authoritative
    // World location so IO-thread handlers (chat) can route Local events
    // without crossing into the sim thread; the sim thread keeps it in
    // sync via SessionManager::update_character_location() when an action
    // moves the character.
    std::optional<uint64_t> character_id;
    uint32_t location_id = 0;
};

} // namespace game::session
