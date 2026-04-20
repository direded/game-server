#pragma once

#include "auth/auth_store.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace game::auth {

// In-memory IAuthStore. State is lost on process restart — fine for step 004
// (unit tests, manual bring-up); step 005 replaces this with Postgres.
class InMemoryAuthStore : public IAuthStore {
public:
    std::optional<AccountRecord> find_account_by_username(std::string_view username) override;
    std::optional<AccountRecord> find_account_by_id(AccountId id) override;
    AccountRecord create_account(std::string_view username,
                                 std::string_view password_hash,
                                 std::string_view email) override;

    SessionRecord create_session(AccountId account_id,
                                 std::string token,
                                 Clock::time_point created_at,
                                 Clock::time_point expires_at) override;
    std::optional<SessionRecord> find_session(std::string_view token) override;
    void touch_session(std::string_view token,
                       Clock::time_point last_seen_at,
                       Clock::time_point expires_at) override;
    void delete_session(std::string_view token) override;
    void delete_sessions_for_account(AccountId account_id) override;

    // Test helper — account count without locking semantics leaking to tests.
    size_t account_count() const;
    size_t session_count() const;

private:
    static std::string to_lower(std::string_view s);

    mutable std::mutex mu_;
    std::atomic<AccountId> next_account_id_{1};

    // username lowercased → account. Case-insensitive uniqueness per spec.
    std::unordered_map<std::string, AccountRecord> accounts_by_username_;
    std::unordered_map<std::string, SessionRecord> sessions_by_token_;
};

} // namespace game::auth
