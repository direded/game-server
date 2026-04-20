#include "auth/auth_rate_limiter.h"
#include "auth/auth_service.h"
#include "auth/in_memory_auth_store.h"
#include "net/framing.h"
#include "protocol/generated/auth_generated.h"
#include "session/session_manager.h"

#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using game::auth::AuthRateLimiter;
using game::auth::AuthService;
using game::auth::InMemoryAuthStore;
using game::net::ConnId;
using game::session::SessionManager;

namespace {

// Capture framed bytes that the service hands back. Each element is one
// complete wire frame (packet_id header + flatbuffers payload).
struct Captured {
    ConnId conn;
    std::vector<uint8_t> bytes;
};

// Test fixture threading the dependencies through the right lifetimes.
class AuthServiceTest : public ::testing::Test {
protected:
    AuthServiceTest()
        : rate_limiter_(AuthRateLimiter::Config{}),
          service_(store_, sessions_, rate_limiter_,
                   [this](ConnId c, const uint8_t* d, size_t l) {
                       captured_.push_back({c, std::vector<uint8_t>(d, d + l)});
                   },
                   AuthService::Config{}) {}

    void register_conn(ConnId c, const std::string& remote) {
        sessions_.add(c, remote);
    }

    void do_register(ConnId c, const std::string& u, const std::string& p, const std::string& e) {
        flatbuffers::FlatBufferBuilder fbb;
        auto off = ::auth::CreateRegister(fbb, fbb.CreateString(u),
                                          fbb.CreateString(p), fbb.CreateString(e));
        fbb.Finish(off);
        service_.handle_register(c, *flatbuffers::GetRoot<::auth::Register>(fbb.GetBufferPointer()));
    }

    void do_login(ConnId c, const std::string& u, const std::string& p) {
        flatbuffers::FlatBufferBuilder fbb;
        auto off = ::auth::CreateLogin(fbb, fbb.CreateString(u), fbb.CreateString(p));
        fbb.Finish(off);
        service_.handle_login(c, *flatbuffers::GetRoot<::auth::Login>(fbb.GetBufferPointer()));
    }

    void do_resume(ConnId c, const std::string& token) {
        flatbuffers::FlatBufferBuilder fbb;
        auto off = ::auth::CreateResume(fbb, fbb.CreateString(token));
        fbb.Finish(off);
        service_.handle_resume(c, *flatbuffers::GetRoot<::auth::Resume>(fbb.GetBufferPointer()));
    }

    // Returns the packet_id of the most recent captured frame (assumes ≥1).
    uint32_t last_packet_id() const {
        const auto& last = captured_.back().bytes;
        auto f = game::net::decode(last.data(), last.size());
        return f->packet_id;
    }

    // Extract the AuthOk token from the last captured frame (must be AuthOk).
    std::string last_auth_ok_token() const {
        const auto& last = captured_.back().bytes;
        auto f = game::net::decode(last.data(), last.size());
        const auto* ok = flatbuffers::GetRoot<::auth::AuthOk>(f->payload);
        return ok->token()->str();
    }

    // Extract the AuthFail reason from the last captured frame (must be AuthFail).
    ::auth::AuthFailReason last_auth_fail_reason() const {
        const auto& last = captured_.back().bytes;
        auto f = game::net::decode(last.data(), last.size());
        const auto* fail = flatbuffers::GetRoot<::auth::AuthFail>(f->payload);
        return fail->reason();
    }

    InMemoryAuthStore store_;
    SessionManager sessions_;
    AuthRateLimiter rate_limiter_;
    AuthService service_;
    std::vector<Captured> captured_;
};

constexpr uint32_t kAuthOkId   = game::net::packet_id("auth.AuthOk");
constexpr uint32_t kAuthFailId = game::net::packet_id("auth.AuthFail");

}  // namespace

// ─── Register ─────────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, RegisterHappyPathSendsAuthOkWithToken) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "alice@example.com");

    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(last_packet_id(), kAuthOkId);
    EXPECT_EQ(last_auth_ok_token().size(), 64u);  // 32 bytes → 64 hex chars
    EXPECT_EQ(store_.account_count(), 1u);
    EXPECT_EQ(store_.session_count(), 1u);

    // Session should be marked authenticated.
    auto s = sessions_.get(1);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->state, game::session::AuthState::Authenticated);
}

TEST_F(AuthServiceTest, RegisterAllowsEmptyEmail) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "");
    EXPECT_EQ(last_packet_id(), kAuthOkId);
}

TEST_F(AuthServiceTest, RegisterRejectsShortUsername) {
    register_conn(1, "127.0.0.1");
    do_register(1, "ab", "password1", "");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_UsernameInvalid);
}

TEST_F(AuthServiceTest, RegisterRejectsUsernameWithBadCharacters) {
    register_conn(1, "127.0.0.1");
    do_register(1, "bad user!", "password1", "");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_UsernameInvalid);
}

TEST_F(AuthServiceTest, RegisterRejectsShortPassword) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "short", "");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_PasswordTooShort);
}

TEST_F(AuthServiceTest, RegisterRejectsMalformedEmail) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "not-an-email");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_EmailInvalid);
}

TEST_F(AuthServiceTest, RegisterRejectsDuplicateUsername) {
    register_conn(1, "127.0.0.1");
    register_conn(2, "127.0.0.2");
    do_register(1, "alice", "password1", "");
    captured_.clear();

    do_register(2, "alice", "password2", "");
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_UsernameTaken);
}

TEST_F(AuthServiceTest, RegisterDuplicateUsernameIsCaseInsensitive) {
    register_conn(1, "127.0.0.1");
    register_conn(2, "127.0.0.2");
    do_register(1, "Alice", "password1", "");
    captured_.clear();

    do_register(2, "alice", "password2", "");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_UsernameTaken);
}

// ─── Login ────────────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, LoginSucceedsAfterRegister) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "");
    captured_.clear();

    register_conn(2, "127.0.0.2");
    do_login(2, "alice", "password1");
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(last_packet_id(), kAuthOkId);
    EXPECT_EQ(last_auth_ok_token().size(), 64u);
}

TEST_F(AuthServiceTest, LoginFailsOnWrongPassword) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "");
    captured_.clear();

    register_conn(2, "127.0.0.2");
    do_login(2, "alice", "wrongpw!");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_InvalidCredentials);
}

TEST_F(AuthServiceTest, LoginFailsOnUnknownUser) {
    register_conn(1, "127.0.0.1");
    do_login(1, "ghost", "password1");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_InvalidCredentials);
}

TEST_F(AuthServiceTest, LoginAccountLocksOutAfterManyFailures) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "");
    captured_.clear();

    // Use per-attempt different IPs so IP bucket doesn't trip first.
    for (size_t i = 0; i < 20; ++i) {
        ConnId c = static_cast<ConnId>(100 + i);
        register_conn(c, "10.0.0." + std::to_string(i));
        do_login(c, "alice", "wrongpw!");
    }

    // 21st attempt from a fresh IP should still be rejected — account-locked.
    register_conn(500, "10.1.2.3");
    captured_.clear();
    do_login(500, "alice", "password1");  // even CORRECT password is locked out
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_RateLimited);
}

TEST_F(AuthServiceTest, LoginSuccessClearsFailCounter) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "");

    // Rack up 19 failed logins (just under the lockout threshold).
    for (size_t i = 0; i < 19; ++i) {
        ConnId c = static_cast<ConnId>(200 + i);
        register_conn(c, "10.0.0." + std::to_string(i));
        do_login(c, "alice", "wrong");
    }

    // Successful login — counter should reset.
    register_conn(900, "10.0.99.99");
    do_login(900, "alice", "password1");
    ASSERT_EQ(game::net::decode(captured_.back().bytes.data(), captured_.back().bytes.size())->packet_id, kAuthOkId);

    // 19 more fails from fresh IPs should NOT lock out the account — the
    // counter was cleared by the successful login above.
    for (size_t i = 0; i < 19; ++i) {
        ConnId c = static_cast<ConnId>(300 + i);
        register_conn(c, "11.0.0." + std::to_string(i));
        do_login(c, "alice", "wrong");
    }

    register_conn(999, "11.0.99.99");
    captured_.clear();
    do_login(999, "alice", "password1");
    EXPECT_EQ(last_packet_id(), kAuthOkId);  // still not locked
}

// ─── IP rate limit ─────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, IpRateLimitKicksInAfterBurst) {
    const std::string ip = "127.0.0.1";
    // 10 Register requests from the same IP → 11th gets RateLimited.
    for (size_t i = 0; i < 10; ++i) {
        ConnId c = static_cast<ConnId>(10 + i);
        register_conn(c, ip);
        do_register(c, "user" + std::to_string(i), "password1", "");
    }

    register_conn(1000, ip);
    captured_.clear();
    do_register(1000, "user_late", "password1", "");
    ASSERT_EQ(captured_.size(), 1u);
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_RateLimited);
}

TEST_F(AuthServiceTest, IpRateLimitDoesNotApplyToResume) {
    // Resume has no IP bucket per spec. Fire 100 Resumes from the same IP —
    // all should return either AuthOk or TokenUnknown (never RateLimited).
    register_conn(1, "127.0.0.1");
    for (size_t i = 0; i < 100; ++i) {
        register_conn(static_cast<ConnId>(100 + i), "127.0.0.1");
        do_resume(static_cast<ConnId>(100 + i), "bogus-token-" + std::to_string(i));
        EXPECT_EQ(last_packet_id(), kAuthFailId);
        EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_TokenUnknown);
    }
}

// ─── Resume ───────────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, ResumeUnknownTokenFails) {
    register_conn(1, "127.0.0.1");
    do_resume(1, "not-a-real-token");
    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_TokenUnknown);
}

TEST_F(AuthServiceTest, ResumeWithValidTokenSucceeds) {
    register_conn(1, "127.0.0.1");
    do_register(1, "alice", "password1", "");
    const std::string token = last_auth_ok_token();
    captured_.clear();

    // New connection — client reconnected after a drop.
    register_conn(2, "127.0.0.1");
    do_resume(2, token);
    EXPECT_EQ(last_packet_id(), kAuthOkId);
    EXPECT_EQ(last_auth_ok_token(), token);

    auto s = sessions_.get(2);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->state, game::session::AuthState::Authenticated);
}

TEST_F(AuthServiceTest, ResumeExtendsExpiryWindow) {
    // Sliding session: a successful Resume pushes expires_at forward so an
    // active client keeps its token indefinitely. Seed a token that would
    // expire in 10 minutes, Resume it, then verify the stored expires_at is
    // now (now + session_ttl), not the original 10-minute horizon.
    auto acct = store_.create_account("alice", "password1", "");
    using Clock = game::auth::Clock;
    const auto seed_now = Clock::now();
    const auto near_exp = seed_now + std::chrono::minutes(10);
    store_.create_session(acct.id, "tok", seed_now, near_exp);

    register_conn(1, "127.0.0.1");
    do_resume(1, "tok");
    ASSERT_EQ(last_packet_id(), kAuthOkId);

    auto updated = store_.find_session("tok");
    ASSERT_TRUE(updated.has_value());
    // New expires_at must be well past the original 10-minute horizon —
    // default session_ttl in AuthService::Config is 30 days.
    EXPECT_GT(updated->expires_at, near_exp + std::chrono::hours(24));
    EXPECT_GT(updated->last_seen_at, seed_now);
}

TEST_F(AuthServiceTest, ResumeWithExpiredTokenFails) {
    // Seed an already-expired session directly into the store.
    auto acct = store_.create_account("alice", "password1", "");
    using Clock = game::auth::Clock;
    const auto past = Clock::now() - std::chrono::hours(1);
    store_.create_session(acct.id, "old-token", past - std::chrono::hours(1), past);

    register_conn(1, "127.0.0.1");
    do_resume(1, "old-token");

    EXPECT_EQ(last_packet_id(), kAuthFailId);
    EXPECT_EQ(last_auth_fail_reason(), ::auth::AuthFailReason_TokenExpired);
    // Expired token should have been purged.
    EXPECT_EQ(store_.session_count(), 0u);
}

// ─── Validation helpers (unit-level) ───────────────────────────────────────

TEST(AuthServiceValidation, UsernameValidation) {
    EXPECT_TRUE(AuthService::is_valid_username("alice"));
    EXPECT_TRUE(AuthService::is_valid_username("Alice_42"));
    EXPECT_TRUE(AuthService::is_valid_username("a-b-c"));
    EXPECT_FALSE(AuthService::is_valid_username("ab"));              // too short
    EXPECT_FALSE(AuthService::is_valid_username(std::string(33, 'a')));  // too long
    EXPECT_FALSE(AuthService::is_valid_username("bad user"));        // space
    EXPECT_FALSE(AuthService::is_valid_username("bad!"));             // punctuation
    EXPECT_FALSE(AuthService::is_valid_username(""));
}

TEST(AuthServiceValidation, PasswordValidation) {
    EXPECT_TRUE(AuthService::is_valid_password("password1"));
    EXPECT_TRUE(AuthService::is_valid_password(std::string(128, 'x')));
    EXPECT_FALSE(AuthService::is_valid_password("short"));
    EXPECT_FALSE(AuthService::is_valid_password(std::string(129, 'x')));
    EXPECT_FALSE(AuthService::is_valid_password(""));
}

TEST(AuthServiceValidation, EmailValidation) {
    EXPECT_TRUE(AuthService::is_valid_email("a@b.c"));
    EXPECT_TRUE(AuthService::is_valid_email("alice@example.com"));
    EXPECT_FALSE(AuthService::is_valid_email(""));
    EXPECT_FALSE(AuthService::is_valid_email("no-at-sign"));
    EXPECT_FALSE(AuthService::is_valid_email("@example.com"));
    EXPECT_FALSE(AuthService::is_valid_email("alice@"));
    EXPECT_FALSE(AuthService::is_valid_email("alice@nodot"));
}

TEST(AuthServiceValidation, GeneratedTokenIs64HexChars) {
    const auto t = AuthService::generate_token();
    EXPECT_EQ(t.size(), 64u);
    for (char c : t) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        EXPECT_TRUE(ok) << "non-hex char: " << c;
    }
}

TEST(AuthServiceValidation, GeneratedTokensDiffer) {
    EXPECT_NE(AuthService::generate_token(), AuthService::generate_token());
}
