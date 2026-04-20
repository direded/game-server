// Integration tests for CharacterStore. Require a live Postgres instance
// whose URL is supplied via the TEST_DATABASE_URL env var and that has
// already had the step-007 migrations applied. Without the env var, every
// test in this file is SKIPPED so CI without a DB still goes green.
//
// WARNING — these tests TRUNCATE the `accounts`, `sessions`, and `characters`
// tables in the target database. Never point TEST_DATABASE_URL at a
// production database; use a dedicated test database.

#include "auth/postgres_auth_store.h"
#include "db/connection.h"
#include "world/character_store.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

using game::auth::PostgresAuthStore;
using game::db::Connection;
using game::world::Character;
using game::world::CharacterStore;
using game::world::is_valid_character_name;

namespace {

const char* test_database_url() {
    return std::getenv("TEST_DATABASE_URL");
}

class CharacterStoreTest : public ::testing::Test {
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

        try {
            conn_->exec("SELECT 1 FROM accounts   LIMIT 1");
            conn_->exec("SELECT 1 FROM characters LIMIT 1");
        } catch (const std::exception& e) {
            FAIL() << "Required tables missing from TEST_DATABASE_URL ("
                   << e.what() << "). Run scripts/db-migrate.ps1 up against "
                      "the test DB first.";
        }

        // CASCADE clears characters and sessions (FK → accounts).
        conn_->exec("TRUNCATE accounts, sessions, characters RESTART IDENTITY CASCADE");

        accounts_ = std::make_unique<PostgresAuthStore>(*conn_);
        store_    = std::make_unique<CharacterStore>(*conn_);
    }

    // Helper: create a real account row (characters has FK → accounts).
    game::session::AccountId make_account(const std::string& username) {
        auto a = accounts_->create_account(username, "$argon2id$fakehash", "");
        return a.id;
    }

    std::unique_ptr<Connection>         conn_;
    std::unique_ptr<PostgresAuthStore>  accounts_;
    std::unique_ptr<CharacterStore>     store_;
};

}  // namespace

// ─── name validation (pure, no DB) ────────────────────────────────────────

TEST(CharacterStoreNames, AcceptsTypicalNames) {
    EXPECT_TRUE(is_valid_character_name("Alice"));
    EXPECT_TRUE(is_valid_character_name("bob"));
    EXPECT_TRUE(is_valid_character_name("Sir Reginald III"));
    EXPECT_TRUE(is_valid_character_name("a-b_c"));
    EXPECT_TRUE(is_valid_character_name("abc"));                    // exactly 3
    EXPECT_TRUE(is_valid_character_name(std::string(24, 'a')));     // exactly 24
}

TEST(CharacterStoreNames, RejectsBadNames) {
    EXPECT_FALSE(is_valid_character_name(""));
    EXPECT_FALSE(is_valid_character_name("ab"));                    // < 3
    EXPECT_FALSE(is_valid_character_name(std::string(25, 'a')));    // > 24
    EXPECT_FALSE(is_valid_character_name("1abc"));                  // first must be letter
    EXPECT_FALSE(is_valid_character_name("-abc"));                  // first must be letter
    EXPECT_FALSE(is_valid_character_name(" abc"));                  // first must be letter
    EXPECT_FALSE(is_valid_character_name("abc!"));                  // illegal char
    EXPECT_FALSE(is_valid_character_name("abc\n"));                 // illegal char
}

// ─── create / find_by_id ──────────────────────────────────────────────────

TEST_F(CharacterStoreTest, CreateReturnsPopulatedRecord) {
    auto acct = make_account("alice");
    auto c = store_->create(acct, "Alice", /*spawn=*/1);
    EXPECT_GT(c.id, 0u);
    EXPECT_EQ(c.account_id, acct);
    EXPECT_EQ(c.name, "Alice");
    EXPECT_EQ(c.location_id, 1u);
    EXPECT_FALSE(c.current_action.has_value());
}

TEST_F(CharacterStoreTest, FindByIdRoundTrips) {
    auto acct = make_account("alice");
    auto c = store_->create(acct, "Alice", 7);

    auto got = store_->find_by_id(c.id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->id, c.id);
    EXPECT_EQ(got->account_id, acct);
    EXPECT_EQ(got->name, "Alice");
    EXPECT_EQ(got->location_id, 7u);
}

TEST_F(CharacterStoreTest, FindByIdMissingReturnsNullopt) {
    EXPECT_FALSE(store_->find_by_id(424242).has_value());
}

// ─── uniqueness ───────────────────────────────────────────────────────────

TEST_F(CharacterStoreTest, DuplicateNameThrows) {
    auto a = make_account("alice");
    auto b = make_account("bob");
    store_->create(a, "Reginald", 1);
    // Same owner, same name — should throw.
    EXPECT_THROW(store_->create(a, "Reginald", 1), std::exception);
    // Different owner, same name — also throws (name UNIQUE is global).
    EXPECT_THROW(store_->create(b, "Reginald", 1), std::exception);
}

TEST_F(CharacterStoreTest, NameUniquenessIsCaseInsensitive) {
    auto a = make_account("alice");
    store_->create(a, "Reginald", 1);
    // citext column → conflicting case must still fail.
    EXPECT_THROW(store_->create(a, "REGINALD", 1), std::exception);
    EXPECT_THROW(store_->create(a, "reginald", 1), std::exception);
}

// ─── list_by_account / load_all ───────────────────────────────────────────

TEST_F(CharacterStoreTest, ListByAccountFiltersByOwner) {
    auto a = make_account("alice");
    auto b = make_account("bob");
    store_->create(a, "Alice1", 1);
    store_->create(a, "Alice2", 2);
    store_->create(b, "BobOnly", 1);

    auto alices = store_->list_by_account(a);
    ASSERT_EQ(alices.size(), 2u);
    // ORDER BY id — first inserted comes first.
    EXPECT_EQ(alices[0].name, "Alice1");
    EXPECT_EQ(alices[1].name, "Alice2");

    auto bobs = store_->list_by_account(b);
    ASSERT_EQ(bobs.size(), 1u);
    EXPECT_EQ(bobs[0].name, "BobOnly");
}

TEST_F(CharacterStoreTest, ListByAccountEmptyForUnknown) {
    EXPECT_TRUE(store_->list_by_account(999999).empty());
}

TEST_F(CharacterStoreTest, LoadAllReturnsEverything) {
    auto a = make_account("alice");
    auto b = make_account("bob");
    store_->create(a, "Alice1", 1);
    store_->create(a, "Alice2", 2);
    store_->create(b, "BobOnly", 3);

    auto all = store_->load_all();
    ASSERT_EQ(all.size(), 3u);

    // Sort deterministically before asserting — load_all has no ORDER BY.
    std::sort(all.begin(), all.end(), [](const Character& x, const Character& y) {
        return x.name < y.name;
    });
    EXPECT_EQ(all[0].name, "Alice1");
    EXPECT_EQ(all[1].name, "Alice2");
    EXPECT_EQ(all[2].name, "BobOnly");
}

TEST_F(CharacterStoreTest, LoadAllEmptyTable) {
    EXPECT_TRUE(store_->load_all().empty());
}

// ─── FK / cascade ─────────────────────────────────────────────────────────

TEST_F(CharacterStoreTest, AccountDeleteCascadesCharacters) {
    auto a = make_account("alice");
    store_->create(a, "Alice1", 1);
    store_->create(a, "Alice2", 2);

    conn_->exec_params("DELETE FROM accounts WHERE id = $1::bigint",
                       {std::to_string(a)});

    EXPECT_TRUE(store_->list_by_account(a).empty());
    EXPECT_TRUE(store_->load_all().empty());
}

// ─── SQL-injection resistance ─────────────────────────────────────────────

TEST_F(CharacterStoreTest, CharacterNameWithSqlMetaCharactersIsStoredVerbatim) {
    auto a = make_account("alice");
    // The character name validator would normally reject this, but the store
    // itself must still treat input as data — guarantees parameterization is
    // in place even if a future caller forgets to validate.
    const std::string evil = "Bobby'; DROP TABLE characters; --";
    auto c = store_->create(a, evil, 1);
    EXPECT_EQ(c.name, evil);

    auto got = store_->find_by_id(c.id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->name, evil);

    // Table must still exist.
    EXPECT_NO_THROW(conn_->exec("SELECT 1 FROM characters LIMIT 1"));
}
