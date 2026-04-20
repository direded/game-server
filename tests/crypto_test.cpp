#include "auth/crypto.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

using game::auth::crypto::generate_session_token;
using game::auth::crypto::hash_password;
using game::auth::crypto::init;
using game::auth::crypto::verify_password;

// ─── init ──────────────────────────────────────────────────────────────────

TEST(Crypto, InitIsIdempotent) {
    // sodium_init returns 1 on second call (already initialized). crypto::init
    // must not throw on repeated calls.
    init();
    init();
    init();
    SUCCEED();
}

// ─── hash / verify ─────────────────────────────────────────────────────────

TEST(Crypto, HashedPasswordHasArgon2idPrefix) {
    // PHC-string contract: Argon2id hashes start with "$argon2id$".
    const auto h = hash_password("password1");
    EXPECT_EQ(h.rfind("$argon2id$", 0), 0u) << h;
}

TEST(Crypto, HashProducesDifferentHashesForSamePassword) {
    // Because of the random salt, two hashes of the same plaintext must
    // differ. Otherwise salt is broken.
    const auto h1 = hash_password("password1");
    const auto h2 = hash_password("password1");
    EXPECT_NE(h1, h2);
}

TEST(Crypto, VerifyAcceptsCorrectPassword) {
    const auto h = hash_password("correct horse battery staple");
    EXPECT_TRUE(verify_password("correct horse battery staple", h));
}

TEST(Crypto, VerifyRejectsWrongPassword) {
    const auto h = hash_password("password1");
    EXPECT_FALSE(verify_password("password2", h));
    EXPECT_FALSE(verify_password("",          h));
    EXPECT_FALSE(verify_password("Password1", h));  // case-sensitive
}

TEST(Crypto, VerifyRejectsMalformedHash) {
    EXPECT_FALSE(verify_password("password1", "not-a-real-hash"));
    EXPECT_FALSE(verify_password("password1", ""));
    // A bcrypt hash is not Argon2id — must also be rejected.
    EXPECT_FALSE(verify_password("password1",
        "$2y$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy"));
}

TEST(Crypto, VerifyRejectsTamperedHash) {
    // Flip one character in the middle — libsodium must reject.
    auto h = hash_password("password1");
    ASSERT_GT(h.size(), 40u);
    h[h.size() - 3] = (h[h.size() - 3] == 'a') ? 'b' : 'a';
    EXPECT_FALSE(verify_password("password1", h));
}

TEST(Crypto, VerifyAcceptsEmptyPasswordAgainstEmptyPasswordHash) {
    // Argon2id is legal over the empty string. Not a feature we want, but
    // documenting the contract so nobody accidentally "patches" it shut.
    const auto h = hash_password("");
    EXPECT_TRUE(verify_password("", h));
    EXPECT_FALSE(verify_password("not-empty", h));
}

// ─── session tokens ────────────────────────────────────────────────────────

TEST(Crypto, TokenIs64LowerHexChars) {
    const auto t = generate_session_token();
    EXPECT_EQ(t.size(), 64u);
    for (char c : t) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        EXPECT_TRUE(ok) << "non-hex char: " << c << " in " << t;
    }
}

TEST(Crypto, TokensAreUniqueAcrossManyCalls) {
    // With 32 bytes of entropy, collisions are astronomically unlikely.
    // A test harness seeing duplicates means the CSPRNG is broken.
    constexpr size_t N = 1000;
    std::unordered_set<std::string> seen;
    seen.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        auto inserted = seen.insert(generate_session_token()).second;
        EXPECT_TRUE(inserted) << "duplicate token at iteration " << i;
    }
    EXPECT_EQ(seen.size(), N);
}
