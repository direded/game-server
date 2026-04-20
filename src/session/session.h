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
    // character_id comes later — step 007 adds the character model
};

} // namespace game::session
