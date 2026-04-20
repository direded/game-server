#pragma once

#include "auth/auth_store.h"
#include "db/connection.h"

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace game::auth {

// Postgres-backed IAuthStore. All queries use parameterized statements
// (PQexecParams); user input never enters the SQL string directly.
//
// Thread-safety: libpq's PGconn is not safe for concurrent use. This store
// serializes all queries behind an internal mutex — which is acceptable at
// the step-005 scale target (handful of concurrent auth ops) but should be
// replaced with a connection pool if the auth path ever becomes a hot spot.
//
// Ownership: the Connection reference is non-owning; main.cpp owns the
// connection object.
class PostgresAuthStore : public IAuthStore {
public:
    // `conn` must outlive this store.
    explicit PostgresAuthStore(db::Connection& conn);

    std::optional<AccountRecord> find_account_by_username(std::string_view username) override;
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

    // Sweeper hook: deletes every session whose expires_at has passed `now`.
    // Returns the tokens that were deleted so callers (the TokenCache
    // invalidator) can evict matching in-memory entries. Not part of
    // IAuthStore — swept only by session_sweeper.
    std::vector<std::string> delete_expired_sessions(Clock::time_point now);

private:
    db::Connection& conn_;
    mutable std::mutex mu_;
};

}  // namespace game::auth
