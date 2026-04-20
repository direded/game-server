#pragma once

#include "auth/auth_store.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game::auth {

// Wraps an underlying IAuthStore with an in-memory cache of token →
// session metadata. Two wins over hitting the store every time:
//   1. find_session short-circuits the DB after a miss is served once.
//   2. touch_session is coalesced — last_seen_at is updated in cache every
//      call, but only written through to the store at most once per
//      `touch_interval`.
//
// The cache is authoritative for last_seen_at / expires_at between
// coalesced writes. A session_sweeper running on its own thread must call
// invalidate() after deleting a token from the store.
class TokenCache : public IAuthStore {
public:
    struct Config {
        // How long a cache entry stays alive after a find_session miss-fill
        // before we fall back to the store again. Independent from the
        // session's own expires_at.
        std::chrono::seconds cache_ttl{std::chrono::seconds(600)};
        // Minimum gap between store writes for the same token under
        // touch_session. In-memory last_seen_at is still updated every call.
        std::chrono::seconds touch_interval{std::chrono::seconds(300)};
    };

    TokenCache(IAuthStore& underlying, Config cfg);

    // ── IAuthStore passthroughs (no caching of account rows at step 005) ──
    std::optional<AccountRecord> find_account_by_username(std::string_view username) override;
    std::optional<AccountRecord> find_account_by_id(AccountId id) override;
    AccountRecord create_account(std::string_view username,
                                 std::string_view password_hash,
                                 std::string_view email) override;

    // ── session-level cached methods ───────────────────────────────────────
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

    // Cache-only eviction — used by the session sweeper after it has
    // already purged the underlying row. Does not touch the underlying
    // store.
    void invalidate(std::string_view token);
    void invalidate_for_account(AccountId account_id);

    // Test helpers.
    size_t cache_size() const;
    // Overrides the clock used for cache-TTL and touch-coalescing. Returning
    // Clock::time_point. If null, defaults to Clock::now().
    void set_now_for_test(std::function<Clock::time_point()> now_fn);

private:
    struct Entry {
        AccountId account_id;
        Clock::time_point created_at;
        Clock::time_point expires_at;
        Clock::time_point last_seen_at;
        // When this entry was inserted (or last refreshed) from the store —
        // used for cache TTL.
        Clock::time_point cached_at;
        // Last time we wrote through touch_session to the store — used for
        // coalescing.
        Clock::time_point last_flushed_at;
    };

    Clock::time_point now() const;

    IAuthStore& underlying_;
    Config cfg_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, Entry> by_token_;
    // Test-only clock override; nullptr in production.
    std::function<Clock::time_point()> now_fn_;
};

}  // namespace game::auth
