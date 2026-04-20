#include "auth/auth_rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>

using game::auth::AuthRateLimiter;
using Clock = AuthRateLimiter::Clock;

namespace {

AuthRateLimiter::Config default_cfg() {
    AuthRateLimiter::Config c;
    c.ip_bucket_cap = 10;
    c.ip_bucket_refill = std::chrono::seconds(6);
    c.per_account_hourly_fails = 20;
    c.per_account_lockout = std::chrono::seconds(3600);
    c.per_account_window = std::chrono::seconds(3600);
    return c;
}

}  // namespace

// ─── IP bucket ─────────────────────────────────────────────────────────────

TEST(AuthRateLimiter, IpBucketAllowsFirstRequest) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    EXPECT_TRUE(rl.try_consume_ip("1.2.3.4", now));
}

TEST(AuthRateLimiter, IpBucketAllowsFullBurst) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 10; ++i) {
        EXPECT_TRUE(rl.try_consume_ip("1.2.3.4", now)) << "i=" << i;
    }
}

TEST(AuthRateLimiter, IpBucketRejectsAfterBurst) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 10; ++i) rl.try_consume_ip("1.2.3.4", now);
    EXPECT_FALSE(rl.try_consume_ip("1.2.3.4", now));
}

TEST(AuthRateLimiter, IpBucketRefillsOverTime) {
    AuthRateLimiter rl(default_cfg());
    auto t0 = Clock::now();
    for (size_t i = 0; i < 10; ++i) rl.try_consume_ip("1.2.3.4", t0);

    // At +6s one token should have refilled.
    EXPECT_TRUE(rl.try_consume_ip("1.2.3.4", t0 + std::chrono::seconds(6)));
    EXPECT_FALSE(rl.try_consume_ip("1.2.3.4", t0 + std::chrono::seconds(6)));
}

TEST(AuthRateLimiter, IpBucketCapsAtConfiguredMax) {
    AuthRateLimiter rl(default_cfg());
    auto t0 = Clock::now();
    // Idle for 1h → would mathematically refill 600 tokens, but cap is 10.
    for (size_t i = 0; i < 10; ++i) {
        EXPECT_TRUE(rl.try_consume_ip("1.2.3.4", t0 + std::chrono::hours(1))) << "i=" << i;
    }
    EXPECT_FALSE(rl.try_consume_ip("1.2.3.4", t0 + std::chrono::hours(1)));
}

TEST(AuthRateLimiter, IpBucketIsPerIp) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 10; ++i) rl.try_consume_ip("1.2.3.4", now);
    EXPECT_FALSE(rl.try_consume_ip("1.2.3.4", now));
    EXPECT_TRUE(rl.try_consume_ip("5.6.7.8", now));
}

// ─── per-account lockout ───────────────────────────────────────────────────

TEST(AuthRateLimiter, AccountNotLockedInitially) {
    AuthRateLimiter rl(default_cfg());
    EXPECT_FALSE(rl.is_account_locked("alice", Clock::now()));
}

TEST(AuthRateLimiter, AccountLocksAfterThresholdFails) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 19; ++i) rl.record_failed_login("alice", now);
    EXPECT_FALSE(rl.is_account_locked("alice", now));
    rl.record_failed_login("alice", now);  // 20th
    EXPECT_TRUE(rl.is_account_locked("alice", now));
}

TEST(AuthRateLimiter, AccountLockoutIsCaseInsensitive) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 20; ++i) rl.record_failed_login("Alice", now);
    EXPECT_TRUE(rl.is_account_locked("alice", now));
    EXPECT_TRUE(rl.is_account_locked("ALICE", now));
}

TEST(AuthRateLimiter, AccountLockoutExpiresAfter1h) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 20; ++i) rl.record_failed_login("alice", now);
    EXPECT_TRUE(rl.is_account_locked("alice", now + std::chrono::minutes(30)));
    EXPECT_FALSE(rl.is_account_locked("alice", now + std::chrono::hours(1) + std::chrono::seconds(1)));
}

TEST(AuthRateLimiter, ClearFailedLoginsUnlocksImmediately) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 20; ++i) rl.record_failed_login("alice", now);
    ASSERT_TRUE(rl.is_account_locked("alice", now));
    rl.clear_failed_logins("alice");
    EXPECT_FALSE(rl.is_account_locked("alice", now));
}

TEST(AuthRateLimiter, AccountFailsResetAfterWindowElapses) {
    AuthRateLimiter rl(default_cfg());
    auto t0 = Clock::now();
    for (size_t i = 0; i < 19; ++i) rl.record_failed_login("alice", t0);

    // Window rolled — next fail starts a fresh count of 1.
    rl.record_failed_login("alice", t0 + std::chrono::hours(2));
    EXPECT_FALSE(rl.is_account_locked("alice", t0 + std::chrono::hours(2)));
}

TEST(AuthRateLimiter, DifferentAccountsDoNotInterfere) {
    AuthRateLimiter rl(default_cfg());
    auto now = Clock::now();
    for (size_t i = 0; i < 20; ++i) rl.record_failed_login("alice", now);
    EXPECT_TRUE(rl.is_account_locked("alice", now));
    EXPECT_FALSE(rl.is_account_locked("bob", now));
}

TEST(AuthRateLimiter, PilingFailsOnLockedAccountDoesNotExtendLock) {
    AuthRateLimiter rl(default_cfg());
    auto t0 = Clock::now();
    for (size_t i = 0; i < 20; ++i) rl.record_failed_login("alice", t0);
    // Keep hammering during the lockout — lockout still ends at t0 + 1h,
    // NOT t0 + 1h + (time of last fail). This prevents an attacker from
    // holding a real user locked out indefinitely via failed attempts.
    for (size_t i = 0; i < 100; ++i) {
        rl.record_failed_login("alice", t0 + std::chrono::minutes(i));
    }
    EXPECT_FALSE(rl.is_account_locked("alice", t0 + std::chrono::hours(1) + std::chrono::seconds(1)));
}
