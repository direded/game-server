#include "auth/in_memory_auth_store.h"
#include "auth/token_cache.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>

using game::auth::AccountId;
using game::auth::AccountRecord;
using game::auth::Clock;
using game::auth::IAuthStore;
using game::auth::InMemoryAuthStore;
using game::auth::SessionRecord;
using game::auth::TokenCache;

namespace {

// Wraps InMemoryAuthStore and counts how many times each method was called.
// Lets us assert "no DB round-trip on cache hit" without watching logs.
class CountingStore : public IAuthStore {
public:
    std::optional<AccountRecord> find_account_by_username(std::string_view u) override {
        ++find_account_calls;
        return inner.find_account_by_username(u);
    }
    AccountRecord create_account(std::string_view u, std::string_view h, std::string_view e) override {
        ++create_account_calls;
        return inner.create_account(u, h, e);
    }
    SessionRecord create_session(AccountId a, std::string t,
                                 Clock::time_point c, Clock::time_point x) override {
        ++create_session_calls;
        return inner.create_session(a, std::move(t), c, x);
    }
    std::optional<SessionRecord> find_session(std::string_view t) override {
        ++find_session_calls;
        return inner.find_session(t);
    }
    void touch_session(std::string_view t, Clock::time_point l, Clock::time_point x) override {
        ++touch_session_calls;
        inner.touch_session(t, l, x);
    }
    void delete_session(std::string_view t) override {
        ++delete_session_calls;
        inner.delete_session(t);
    }
    void delete_sessions_for_account(AccountId a) override {
        ++delete_for_account_calls;
        inner.delete_sessions_for_account(a);
    }

    InMemoryAuthStore inner;
    std::atomic<int> find_account_calls{0};
    std::atomic<int> create_account_calls{0};
    std::atomic<int> create_session_calls{0};
    std::atomic<int> find_session_calls{0};
    std::atomic<int> touch_session_calls{0};
    std::atomic<int> delete_session_calls{0};
    std::atomic<int> delete_for_account_calls{0};
};

// Fresh store + cache per test.
struct Harness {
    CountingStore under;
    TokenCache cache;
    Clock::time_point fake_now = Clock::now();
    TokenCache::Config cfg;

    explicit Harness(TokenCache::Config c = {})
        : cache(under, c), cfg(c) {
        cache.set_now_for_test([this]() { return fake_now; });
    }

    SessionRecord seed_session(AccountId account_id,
                               const std::string& token,
                               std::chrono::seconds ttl) {
        return cache.create_session(account_id, token, fake_now, fake_now + ttl);
    }
};

}  // namespace

// ─── passthrough: account operations always hit the underlying store ──────

TEST(TokenCache, AccountOperationsPassThrough) {
    Harness h;
    auto acct = h.cache.create_account("alice", "hash1", "");
    EXPECT_EQ(h.under.create_account_calls, 1);

    auto found = h.cache.find_account_by_username("alice");
    EXPECT_EQ(h.under.find_account_calls, 1);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, acct.id);
}

// ─── find_session caches after a miss ──────────────────────────────────────

TEST(TokenCache, FindSessionMissFetchesFromStoreAndPopulatesCache) {
    Harness h;
    auto acct = h.cache.create_account("alice", "h", "");
    // Bypass the cache.create_session so we can observe a clean miss:
    h.under.inner.create_session(acct.id, "tok-1", h.fake_now,
                                 h.fake_now + std::chrono::hours(1));
    EXPECT_EQ(h.cache.cache_size(), 0u);

    auto got = h.cache.find_session("tok-1");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->account_id, acct.id);
    EXPECT_EQ(h.under.find_session_calls, 1);
    EXPECT_EQ(h.cache.cache_size(), 1u);
}

TEST(TokenCache, SecondFindSessionWithinTtlIsACacheHit) {
    Harness h;
    auto acct = h.cache.create_account("alice", "h", "");
    h.seed_session(acct.id, "tok-1", std::chrono::hours(1));
    EXPECT_EQ(h.under.create_session_calls, 1);  // warmed by create_session

    EXPECT_EQ(h.under.find_session_calls, 0);
    h.cache.find_session("tok-1");
    EXPECT_EQ(h.under.find_session_calls, 0) << "create_session should have primed the cache";

    // Advance time but stay inside cache_ttl (default 600s).
    h.fake_now += std::chrono::seconds(10);
    h.cache.find_session("tok-1");
    EXPECT_EQ(h.under.find_session_calls, 0);
}

TEST(TokenCache, FindSessionAfterCacheTtlFetchesAgain) {
    TokenCache::Config c;
    c.cache_ttl = std::chrono::seconds(30);
    Harness h(c);
    auto acct = h.cache.create_account("alice", "h", "");
    h.seed_session(acct.id, "tok-1", std::chrono::hours(1));

    h.cache.find_session("tok-1");
    EXPECT_EQ(h.under.find_session_calls, 0);

    // Advance past cache_ttl → cache entry considered stale → store call.
    h.fake_now += std::chrono::seconds(31);
    h.cache.find_session("tok-1");
    EXPECT_EQ(h.under.find_session_calls, 1);
}

TEST(TokenCache, FindSessionForUnknownTokenReturnsNulloptWithoutCaching) {
    Harness h;
    auto got = h.cache.find_session("nope");
    EXPECT_FALSE(got.has_value());
    EXPECT_EQ(h.under.find_session_calls, 1);
    EXPECT_EQ(h.cache.cache_size(), 0u);
}

// ─── touch_session coalescing ──────────────────────────────────────────────

TEST(TokenCache, TouchSessionCoalescesWrites) {
    TokenCache::Config c;
    c.touch_interval = std::chrono::seconds(60);
    Harness h(c);
    auto acct = h.cache.create_account("alice", "h", "");
    h.seed_session(acct.id, "tok-1", std::chrono::hours(1));

    // First touch flushes immediately because we just created the entry —
    // last_flushed_at == cached_at, since_flush == 0 → < touch_interval → no flush.
    // So the first touch within the interval should NOT flush.
    h.fake_now += std::chrono::seconds(5);
    h.cache.touch_session("tok-1", h.fake_now, h.fake_now + std::chrono::hours(1));
    EXPECT_EQ(h.under.touch_session_calls, 0);

    h.fake_now += std::chrono::seconds(5);
    h.cache.touch_session("tok-1", h.fake_now, h.fake_now + std::chrono::hours(1));
    EXPECT_EQ(h.under.touch_session_calls, 0);

    // Advance past touch_interval — next touch writes through.
    h.fake_now += std::chrono::seconds(61);
    h.cache.touch_session("tok-1", h.fake_now, h.fake_now + std::chrono::hours(1));
    EXPECT_EQ(h.under.touch_session_calls, 1);

    // And a touch immediately after the flush stays coalesced again.
    h.fake_now += std::chrono::seconds(2);
    h.cache.touch_session("tok-1", h.fake_now, h.fake_now + std::chrono::hours(1));
    EXPECT_EQ(h.under.touch_session_calls, 1);
}

TEST(TokenCache, TouchSessionOnUncachedTokenWritesThrough) {
    // A token the cache has never seen should always write through —
    // otherwise data silently goes missing.
    Harness h;
    auto acct = h.cache.create_account("alice", "h", "");
    h.under.inner.create_session(acct.id, "tok-1", h.fake_now,
                                 h.fake_now + std::chrono::hours(1));

    h.cache.touch_session("tok-1", h.fake_now, h.fake_now + std::chrono::hours(1));
    EXPECT_EQ(h.under.touch_session_calls, 1);
}

TEST(TokenCache, TouchSessionUpdatesCacheEvenWhenCoalesced) {
    TokenCache::Config c;
    c.touch_interval = std::chrono::seconds(60);
    Harness h(c);
    auto acct = h.cache.create_account("alice", "h", "");
    h.seed_session(acct.id, "tok-1", std::chrono::hours(1));

    const auto new_expiry = h.fake_now + std::chrono::hours(24);
    h.fake_now += std::chrono::seconds(10);
    h.cache.touch_session("tok-1", h.fake_now, new_expiry);
    EXPECT_EQ(h.under.touch_session_calls, 0);

    // find_session should read the cache-updated expiry, not the original.
    auto got = h.cache.find_session("tok-1");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->expires_at, new_expiry);
    EXPECT_EQ(got->last_seen_at, h.fake_now);
    EXPECT_EQ(h.under.find_session_calls, 0);  // still cached
}

// ─── delete / invalidate ───────────────────────────────────────────────────

TEST(TokenCache, DeleteSessionEvictsCacheAndStore) {
    Harness h;
    auto acct = h.cache.create_account("alice", "h", "");
    h.seed_session(acct.id, "tok-1", std::chrono::hours(1));
    EXPECT_EQ(h.cache.cache_size(), 1u);

    h.cache.delete_session("tok-1");
    EXPECT_EQ(h.under.delete_session_calls, 1);
    EXPECT_EQ(h.cache.cache_size(), 0u);

    // Subsequent find_session must go to the store (miss) and return nullopt.
    EXPECT_FALSE(h.cache.find_session("tok-1").has_value());
    EXPECT_EQ(h.under.find_session_calls, 1);
}

TEST(TokenCache, DeleteSessionsForAccountEvictsAllMatching) {
    Harness h;
    auto alice = h.cache.create_account("alice", "h", "");
    auto bob   = h.cache.create_account("bob",   "h", "");
    h.seed_session(alice.id, "t-alice-1", std::chrono::hours(1));
    h.seed_session(alice.id, "t-alice-2", std::chrono::hours(1));
    h.seed_session(bob.id,   "t-bob-1",   std::chrono::hours(1));

    h.cache.delete_sessions_for_account(alice.id);
    EXPECT_EQ(h.under.delete_for_account_calls, 1);

    // Alice's tokens evicted from cache; Bob's still cached.
    EXPECT_EQ(h.cache.cache_size(), 1u);
    EXPECT_FALSE(h.under.inner.find_session("t-alice-1").has_value());
    EXPECT_TRUE (h.under.inner.find_session("t-bob-1"  ).has_value());
}

TEST(TokenCache, InvalidateEvictsCacheOnlyWithoutTouchingStore) {
    // Session sweeper already deleted rows in bulk — it calls invalidate()
    // rather than delete_session() to avoid a redundant DELETE.
    Harness h;
    auto acct = h.cache.create_account("alice", "h", "");
    h.seed_session(acct.id, "tok-1", std::chrono::hours(1));

    h.cache.invalidate("tok-1");
    EXPECT_EQ(h.under.delete_session_calls, 0);
    EXPECT_EQ(h.cache.cache_size(), 0u);
    // Row is still in the underlying store — invalidate is cache-only.
    EXPECT_TRUE(h.under.inner.find_session("tok-1").has_value());
}

TEST(TokenCache, InvalidateForAccountEvictsAllMatchingCacheEntries) {
    Harness h;
    auto alice = h.cache.create_account("alice", "h", "");
    auto bob   = h.cache.create_account("bob",   "h", "");
    h.seed_session(alice.id, "t-alice-1", std::chrono::hours(1));
    h.seed_session(alice.id, "t-alice-2", std::chrono::hours(1));
    h.seed_session(bob.id,   "t-bob-1",   std::chrono::hours(1));

    h.cache.invalidate_for_account(alice.id);
    EXPECT_EQ(h.cache.cache_size(), 1u);
    EXPECT_EQ(h.under.delete_for_account_calls, 0);
}
