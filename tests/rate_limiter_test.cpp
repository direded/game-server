#include "chat/rate_limiter.h"

#include <gtest/gtest.h>

#include <chrono>

using game::chat::Channel;
using game::chat::RateLimiter;
using game::session::AccountId;
using Clock = std::chrono::steady_clock;

namespace {

RateLimiter::Config default_cfg() {
    RateLimiter::Config c;
    c.local  = {5.0, 1.0};   // Local: cap 5, refill 1/sec
    c.global = {3.0, 0.2};   // Global: cap 3, refill 0.2/sec (every 5s)
    return c;
}

}  // namespace

// ─── cap / burst behaviour ─────────────────────────────────────────────────

TEST(ChatRateLimiter, NewAccountBucketStartsFull_Local) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(rl.try_consume(42, Channel::Local, t0)) << "i=" << i;
    }
}

TEST(ChatRateLimiter, LocalBucketRejectsAfterBurst) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) ASSERT_TRUE(rl.try_consume(42, Channel::Local, t0));
    EXPECT_FALSE(rl.try_consume(42, Channel::Local, t0));
}

TEST(ChatRateLimiter, GlobalBucketCapIsSmaller) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 3; ++i) EXPECT_TRUE(rl.try_consume(42, Channel::Global, t0));
    EXPECT_FALSE(rl.try_consume(42, Channel::Global, t0));
}

// ─── refill ─────────────────────────────────────────────────────────────────

TEST(ChatRateLimiter, LocalBucketRefillsOneTokenPerSecond) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) rl.try_consume(42, Channel::Local, t0);
    EXPECT_FALSE(rl.try_consume(42, Channel::Local, t0));

    // +1 second → one token.
    EXPECT_TRUE(rl.try_consume(42, Channel::Local, t0 + std::chrono::seconds(1)));
    EXPECT_FALSE(rl.try_consume(42, Channel::Local, t0 + std::chrono::seconds(1)));
}

TEST(ChatRateLimiter, GlobalBucketRefillsOneTokenPer5Seconds) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 3; ++i) rl.try_consume(42, Channel::Global, t0);

    // After 4s, less than one token refilled.
    EXPECT_FALSE(rl.try_consume(42, Channel::Global, t0 + std::chrono::seconds(4)));
    // After 5s, one full token available.
    EXPECT_TRUE(rl.try_consume(42, Channel::Global, t0 + std::chrono::seconds(5)));
}

TEST(ChatRateLimiter, RefillCapsAtBucketSize) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) rl.try_consume(42, Channel::Local, t0);

    // Idle for an hour — would mathematically refill 3600 tokens but cap is 5.
    const auto later = t0 + std::chrono::hours(1);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(rl.try_consume(42, Channel::Local, later)) << "i=" << i;
    }
    EXPECT_FALSE(rl.try_consume(42, Channel::Local, later));
}

// ─── isolation between keys ─────────────────────────────────────────────────

TEST(ChatRateLimiter, DifferentAccountsHaveIndependentBuckets) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) rl.try_consume(42, Channel::Local, t0);
    EXPECT_FALSE(rl.try_consume(42, Channel::Local, t0));
    // Different account gets its own full bucket.
    EXPECT_TRUE(rl.try_consume(43, Channel::Local, t0));
}

TEST(ChatRateLimiter, LocalAndGlobalBucketsAreIndependentPerAccount) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) rl.try_consume(42, Channel::Local, t0);
    EXPECT_FALSE(rl.try_consume(42, Channel::Local, t0));
    // Same account, other channel — untouched.
    EXPECT_TRUE(rl.try_consume(42, Channel::Global, t0));
}

TEST(ChatRateLimiter, BucketIsLazilyCreated) {
    RateLimiter rl(default_cfg());
    EXPECT_EQ(rl.tracked_keys(), 0u);
    rl.try_consume(1, Channel::Local, Clock::now());
    EXPECT_EQ(rl.tracked_keys(), 1u);
    rl.try_consume(1, Channel::Global, Clock::now());
    EXPECT_EQ(rl.tracked_keys(), 2u);
    rl.try_consume(2, Channel::Local, Clock::now());
    EXPECT_EQ(rl.tracked_keys(), 3u);
}

// ─── retry_after semantics ─────────────────────────────────────────────────

TEST(ChatRateLimiter, RetryAfterZeroWhenBucketHasTokens) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    EXPECT_EQ(rl.retry_after(42, Channel::Local, t0).count(), 0);
}

TEST(ChatRateLimiter, RetryAfterPositiveWhenEmpty_Local) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 5; ++i) rl.try_consume(42, Channel::Local, t0);
    // Local refill 1/s → next token ~1000 ms out.
    const auto ms = rl.retry_after(42, Channel::Local, t0).count();
    EXPECT_GE(ms, 900);
    EXPECT_LE(ms, 1100);
}

TEST(ChatRateLimiter, RetryAfterPositiveWhenEmpty_Global) {
    RateLimiter rl(default_cfg());
    const auto t0 = Clock::now();
    for (int i = 0; i < 3; ++i) rl.try_consume(42, Channel::Global, t0);
    // Global refill 0.2/s → next token ~5000 ms out.
    const auto ms = rl.retry_after(42, Channel::Global, t0).count();
    EXPECT_GE(ms, 4500);
    EXPECT_LE(ms, 5500);
}
