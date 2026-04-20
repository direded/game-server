#include "auth/in_memory_auth_store.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using game::auth::AccountId;
using game::auth::Clock;
using game::auth::InMemoryAuthStore;

namespace {
Clock::time_point now_plus(std::chrono::seconds s) {
    return Clock::now() + s;
}
}  // namespace

// ─── accounts ─────────────────────────────────────────────────────────────

TEST(InMemoryAuthStore, CreateAccountReturnsPopulatedRecord) {
    InMemoryAuthStore s;
    auto acct = s.create_account("alice", "hash1", "alice@example.com");
    EXPECT_GT(acct.id, 0u);
    EXPECT_EQ(acct.username, "alice");
    EXPECT_EQ(acct.password_hash, "hash1");
    EXPECT_EQ(acct.email, "alice@example.com");
    EXPECT_EQ(s.account_count(), 1u);
}

TEST(InMemoryAuthStore, CreateAccountAssignsMonotonicIds) {
    InMemoryAuthStore s;
    auto a1 = s.create_account("alice", "h", "");
    auto a2 = s.create_account("bob", "h", "");
    EXPECT_LT(a1.id, a2.id);
}

TEST(InMemoryAuthStore, FindAccountByExactUsername) {
    InMemoryAuthStore s;
    s.create_account("alice", "h", "");
    auto found = s.find_account_by_username("alice");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->username, "alice");
}

TEST(InMemoryAuthStore, FindAccountIsCaseInsensitive) {
    InMemoryAuthStore s;
    s.create_account("Alice", "h", "");

    EXPECT_TRUE(s.find_account_by_username("alice").has_value());
    EXPECT_TRUE(s.find_account_by_username("ALICE").has_value());
    EXPECT_TRUE(s.find_account_by_username("AlIcE").has_value());
}

TEST(InMemoryAuthStore, FindAccountMissingReturnsNullopt) {
    InMemoryAuthStore s;
    EXPECT_FALSE(s.find_account_by_username("ghost").has_value());
}

// ─── sessions ─────────────────────────────────────────────────────────────

TEST(InMemoryAuthStore, CreateAndFindSession) {
    InMemoryAuthStore s;
    auto acct = s.create_account("alice", "h", "");
    auto now = Clock::now();
    auto expires = now + std::chrono::hours(24);
    s.create_session(acct.id, "tok-1", now, expires);

    auto found = s.find_session("tok-1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->token, "tok-1");
    EXPECT_EQ(found->account_id, acct.id);
    EXPECT_EQ(found->created_at, now);
    EXPECT_EQ(found->expires_at, expires);
    EXPECT_EQ(found->last_seen_at, now);  // last_seen initialized to created_at
    EXPECT_EQ(s.session_count(), 1u);
}

TEST(InMemoryAuthStore, FindSessionMissingReturnsNullopt) {
    InMemoryAuthStore s;
    EXPECT_FALSE(s.find_session("not-a-token").has_value());
}

TEST(InMemoryAuthStore, TouchSessionUpdatesLastSeenAt) {
    InMemoryAuthStore s;
    auto acct = s.create_account("alice", "h", "");
    auto now = Clock::now();
    s.create_session(acct.id, "tok", now, now + std::chrono::hours(1));

    const auto later = now + std::chrono::minutes(10);
    s.touch_session("tok", later);

    auto found = s.find_session("tok");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->last_seen_at, later);
    EXPECT_EQ(found->created_at, now);  // created_at must not move
}

TEST(InMemoryAuthStore, TouchMissingSessionIsNoop) {
    InMemoryAuthStore s;
    s.touch_session("nope", Clock::now());
    EXPECT_EQ(s.session_count(), 0u);
}

TEST(InMemoryAuthStore, DeleteSessionRemovesIt) {
    InMemoryAuthStore s;
    auto acct = s.create_account("alice", "h", "");
    s.create_session(acct.id, "tok", Clock::now(), now_plus(std::chrono::seconds(60)));
    s.delete_session("tok");
    EXPECT_FALSE(s.find_session("tok").has_value());
    EXPECT_EQ(s.session_count(), 0u);
}

TEST(InMemoryAuthStore, DeleteSessionsForAccountRemovesAllMatching) {
    InMemoryAuthStore s;
    auto alice = s.create_account("alice", "h", "");
    auto bob   = s.create_account("bob",   "h", "");
    auto now = Clock::now();
    auto exp = now + std::chrono::hours(1);
    s.create_session(alice.id, "t-alice-1", now, exp);
    s.create_session(alice.id, "t-alice-2", now, exp);
    s.create_session(bob.id,   "t-bob-1",   now, exp);

    s.delete_sessions_for_account(alice.id);

    EXPECT_FALSE(s.find_session("t-alice-1").has_value());
    EXPECT_FALSE(s.find_session("t-alice-2").has_value());
    EXPECT_TRUE(s.find_session("t-bob-1").has_value());
    EXPECT_EQ(s.session_count(), 1u);
}

TEST(InMemoryAuthStore, ExpiryDetectedByCaller) {
    // The store doesn't auto-expire; callers compare expires_at to now.
    // This test documents that contract — find_session returns expired rows,
    // and auth_service is responsible for the expiry check.
    InMemoryAuthStore s;
    auto acct = s.create_account("alice", "h", "");
    auto now = Clock::now();
    auto past = now - std::chrono::seconds(1);
    s.create_session(acct.id, "expired", now - std::chrono::hours(1), past);

    auto found = s.find_session("expired");
    ASSERT_TRUE(found.has_value());
    EXPECT_LT(found->expires_at, now);
}
