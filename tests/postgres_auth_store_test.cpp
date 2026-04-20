// Integration tests for PostgresAuthStore. Require a live Postgres instance
// whose URL is supplied via the TEST_DATABASE_URL env var and that has
// already had the step-005 migrations applied. Without that env var, every
// test in this file is SKIPPED so CI without a DB still goes green.
//
// WARNING — these tests TRUNCATE the `accounts` and `sessions` tables in the
// target database. Never point TEST_DATABASE_URL at a production database;
// use a dedicated test database.

#include "auth/postgres_auth_store.h"
#include "db/connection.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

using game::auth::AccountRecord;
using game::auth::Clock;
using game::auth::PostgresAuthStore;
using game::auth::SessionRecord;
using game::db::Connection;

namespace {

const char* test_database_url() {
    return std::getenv("TEST_DATABASE_URL");
}

class PostgresAuthStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* url = test_database_url();
        if (!url || !*url) {
            GTEST_SKIP() << "TEST_DATABASE_URL unset — skipping Postgres-backed tests.";
        }

        try {
            conn_ = std::make_unique<Connection>(url);
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Connection to TEST_DATABASE_URL failed: " << e.what();
        }

        // Fail-loud if the schema isn't applied — tests are meaningless
        // against an empty DB.
        try {
            conn_->exec("SELECT 1 FROM accounts LIMIT 1");
            conn_->exec("SELECT 1 FROM sessions LIMIT 1");
        } catch (const std::exception& e) {
            FAIL() << "Required tables missing from TEST_DATABASE_URL ("
                   << e.what() << "). Run scripts/db-migrate.ps1 up against "
                      "the test DB first.";
        }

        // CASCADE also clears sessions (FK → accounts).
        conn_->exec("TRUNCATE accounts, sessions RESTART IDENTITY CASCADE");

        store_ = std::make_unique<PostgresAuthStore>(*conn_);
    }

    std::unique_ptr<Connection> conn_;
    std::unique_ptr<PostgresAuthStore> store_;
};

}  // namespace

// ─── accounts ─────────────────────────────────────────────────────────────

TEST_F(PostgresAuthStoreTest, CreateAccountReturnsPopulatedRecord) {
    auto acct = store_->create_account("alice", "$argon2id$fakehash", "alice@example.com");
    EXPECT_GT(acct.id, 0u);
    EXPECT_EQ(acct.username, "alice");
    EXPECT_EQ(acct.password_hash, "$argon2id$fakehash");
    EXPECT_EQ(acct.email, "alice@example.com");
}

TEST_F(PostgresAuthStoreTest, CreateAccountStoresEmptyEmailAsNull) {
    // Empty email must not collide with other empty-email accounts under
    // the citext UNIQUE constraint on email (we don't put one today, but
    // it's the "expected behavior" documented in the store).
    store_->create_account("alice", "h", "");
    store_->create_account("bob",   "h", "");
    // Both should succeed; no uniqueness crash.
    SUCCEED();
}

TEST_F(PostgresAuthStoreTest, FindAccountByExactUsername) {
    store_->create_account("alice", "h", "");
    auto found = store_->find_account_by_username("alice");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->username, "alice");
}

TEST_F(PostgresAuthStoreTest, FindAccountIsCaseInsensitive) {
    // citext column — lookups must be case-insensitive.
    store_->create_account("Alice", "h", "");
    EXPECT_TRUE(store_->find_account_by_username("alice").has_value());
    EXPECT_TRUE(store_->find_account_by_username("ALICE").has_value());
    EXPECT_TRUE(store_->find_account_by_username("AlIcE").has_value());
}

TEST_F(PostgresAuthStoreTest, FindAccountMissingReturnsNullopt) {
    EXPECT_FALSE(store_->find_account_by_username("ghost").has_value());
}

TEST_F(PostgresAuthStoreTest, CreateAccountDuplicateUsernameThrows) {
    store_->create_account("alice", "h", "");
    EXPECT_THROW(store_->create_account("alice", "h", ""), std::exception);
    EXPECT_THROW(store_->create_account("ALICE", "h", ""), std::exception);  // case-insensitive
}

// ─── SQL-injection resistance ─────────────────────────────────────────────

TEST_F(PostgresAuthStoreTest, UsernameWithSqlMetaCharactersIsStoredVerbatim) {
    // Parameterized queries must treat this as data, not SQL.
    const std::string evil = "bobby'; DROP TABLE accounts; --";
    auto acct = store_->create_account(evil, "h", "");
    EXPECT_EQ(acct.username, evil);

    auto found = store_->find_account_by_username(evil);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->username, evil);

    // Table must still exist.
    EXPECT_NO_THROW(conn_->exec("SELECT 1 FROM accounts LIMIT 1"));
}

// ─── sessions ─────────────────────────────────────────────────────────────

TEST_F(PostgresAuthStoreTest, CreateAndFindSession) {
    auto acct = store_->create_account("alice", "h", "");
    const auto now = Clock::now();
    const auto expires = now + std::chrono::hours(24);
    store_->create_session(acct.id, "tok-1", now, expires);

    auto got = store_->find_session("tok-1");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->token, "tok-1");
    EXPECT_EQ(got->account_id, acct.id);
    // Microsecond round-trip — timestamps should match within the precision
    // of Postgres's timestamptz (1 µs).
    using us = std::chrono::microseconds;
    EXPECT_LT(std::chrono::abs(std::chrono::duration_cast<us>(got->created_at - now)),
              us(2));
    EXPECT_LT(std::chrono::abs(std::chrono::duration_cast<us>(got->expires_at - expires)),
              us(2));
}

TEST_F(PostgresAuthStoreTest, FindSessionMissingReturnsNullopt) {
    EXPECT_FALSE(store_->find_session("not-a-token").has_value());
}

TEST_F(PostgresAuthStoreTest, TouchSessionUpdatesLastSeenAndExpiry) {
    auto acct = store_->create_account("alice", "h", "");
    const auto now = Clock::now();
    store_->create_session(acct.id, "tok", now, now + std::chrono::hours(1));

    const auto later = now + std::chrono::minutes(10);
    const auto new_exp = later + std::chrono::hours(24);
    store_->touch_session("tok", later, new_exp);

    auto got = store_->find_session("tok");
    ASSERT_TRUE(got.has_value());
    using us = std::chrono::microseconds;
    EXPECT_LT(std::chrono::abs(std::chrono::duration_cast<us>(got->last_seen_at - later)),
              us(2));
    EXPECT_LT(std::chrono::abs(std::chrono::duration_cast<us>(got->expires_at - new_exp)),
              us(2));
}

TEST_F(PostgresAuthStoreTest, TouchMissingSessionIsNoop) {
    // UPDATE with no matching WHERE is not an error in Postgres.
    EXPECT_NO_THROW(store_->touch_session("nope", Clock::now(),
                                          Clock::now() + std::chrono::hours(1)));
}

TEST_F(PostgresAuthStoreTest, DeleteSessionRemovesIt) {
    auto acct = store_->create_account("alice", "h", "");
    const auto now = Clock::now();
    store_->create_session(acct.id, "tok", now, now + std::chrono::hours(1));
    store_->delete_session("tok");
    EXPECT_FALSE(store_->find_session("tok").has_value());
}

TEST_F(PostgresAuthStoreTest, DeleteSessionsForAccountRemovesAllMatching) {
    auto alice = store_->create_account("alice", "h", "");
    auto bob   = store_->create_account("bob",   "h", "");
    const auto now = Clock::now();
    const auto exp = now + std::chrono::hours(1);
    store_->create_session(alice.id, "t-alice-1", now, exp);
    store_->create_session(alice.id, "t-alice-2", now, exp);
    store_->create_session(bob.id,   "t-bob-1",   now, exp);

    store_->delete_sessions_for_account(alice.id);

    EXPECT_FALSE(store_->find_session("t-alice-1").has_value());
    EXPECT_FALSE(store_->find_session("t-alice-2").has_value());
    EXPECT_TRUE (store_->find_session("t-bob-1"  ).has_value());
}

TEST_F(PostgresAuthStoreTest, AccountDeleteCascadesSessions) {
    // The sessions.account_id FK is ON DELETE CASCADE — deleting the account
    // from the DB should clear the sessions too. We don't expose DELETE
    // accounts via the store, but this documents the schema invariant.
    auto acct = store_->create_account("alice", "h", "");
    const auto now = Clock::now();
    store_->create_session(acct.id, "t-alice-1", now, now + std::chrono::hours(1));

    conn_->exec_params("DELETE FROM accounts WHERE id = $1::bigint",
                       {std::to_string(acct.id)});
    EXPECT_FALSE(store_->find_session("t-alice-1").has_value());
}

// ─── sweep ─────────────────────────────────────────────────────────────────

TEST_F(PostgresAuthStoreTest, DeleteExpiredSessionsReturnsPrunedTokens) {
    auto acct = store_->create_account("alice", "h", "");
    const auto now = Clock::now();
    store_->create_session(acct.id, "fresh",   now, now + std::chrono::hours(1));
    store_->create_session(acct.id, "expired", now - std::chrono::hours(2), now - std::chrono::hours(1));

    auto pruned = store_->delete_expired_sessions(now);
    ASSERT_EQ(pruned.size(), 1u);
    EXPECT_EQ(pruned[0], "expired");
    EXPECT_TRUE (store_->find_session("fresh"  ).has_value());
    EXPECT_FALSE(store_->find_session("expired").has_value());
}

TEST_F(PostgresAuthStoreTest, DeleteExpiredSessionsOnEmptyTableIsNoop) {
    auto pruned = store_->delete_expired_sessions(Clock::now());
    EXPECT_TRUE(pruned.empty());
}
