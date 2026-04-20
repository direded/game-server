#pragma once

#include "session/session.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace game::auth {

using AccountId = session::AccountId;
using Clock = std::chrono::system_clock;

struct AccountRecord {
    AccountId id = 0;
    std::string username;
    // TODO(step-005): replace with Argon2id via libsodium.
    // Plaintext here so the TODO is impossible to miss; a weak placeholder
    // hash would invite someone shipping it to prod.
    std::string password_hash;
    std::string email;
};

struct SessionRecord {
    std::string token;
    AccountId account_id = 0;
    Clock::time_point created_at{};
    Clock::time_point expires_at{};
    Clock::time_point last_seen_at{};
};

// Storage abstraction for accounts and long-lived sessions. Step 005 swaps
// the in-memory impl for a Postgres-backed one without changing this API.
class IAuthStore {
public:
    virtual ~IAuthStore() = default;

    virtual std::optional<AccountRecord> find_account_by_username(std::string_view username) = 0;
    virtual AccountRecord create_account(std::string_view username,
                                         std::string_view password_hash,
                                         std::string_view email) = 0;

    virtual SessionRecord create_session(AccountId account_id,
                                         std::string token,
                                         Clock::time_point created_at,
                                         Clock::time_point expires_at) = 0;
    virtual std::optional<SessionRecord> find_session(std::string_view token) = 0;
    // Sliding session: advances last_seen_at and extends expires_at in one hop.
    virtual void touch_session(std::string_view token,
                               Clock::time_point last_seen_at,
                               Clock::time_point expires_at) = 0;
    virtual void delete_session(std::string_view token) = 0;
    virtual void delete_sessions_for_account(AccountId account_id) = 0;
};

} // namespace game::auth
