#include "auth/auth_service.h"

#include "auth/crypto.h"
#include "log/logger.h"
#include "net/framing.h"
#include "protocol/generated/auth_generated.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <sstream>
#include <iomanip>

namespace game::auth {

namespace {

constexpr size_t kMinUsername = 3;
constexpr size_t kMaxUsername = 32;
constexpr size_t kMinPassword = 8;
constexpr size_t kMaxPassword = 128;

// Show only the first 8 hex chars of a token in logs — enough to correlate
// across events, not enough to reuse if logs leak.
std::string token_fingerprint(std::string_view token) {
    if (token.size() <= 8) return std::string(token);
    return std::string(token.substr(0, 8)) + "...";
}

// Pre-computed Argon2id hash used to keep verify_password's runtime constant
// on the unknown-username path. First call pays one hash (~10 ms); all
// subsequent calls reuse the same string. Lazy so crypto::init() runs first.
const std::string& dummy_password_hash() {
    static const std::string h = crypto::hash_password("timing-defense-dummy");
    return h;
}

bool is_unique_violation(const std::exception& e) {
    std::string_view msg = e.what();
    return msg.find("23505") != std::string_view::npos
        || msg.find("duplicate key") != std::string_view::npos
        || msg.find("unique constraint") != std::string_view::npos;
}

} // namespace

AuthService::AuthService(IAuthStore& store,
                         session::SessionManager& sessions,
                         AuthRateLimiter& rate_limiter,
                         Sender sender,
                         Config cfg)
    : store_(store), sessions_(sessions), rate_limiter_(rate_limiter),
      sender_(std::move(sender)), cfg_(cfg) {}

bool AuthService::is_valid_username(std::string_view s) {
    if (s.size() < kMinUsername || s.size() > kMaxUsername) return false;
    for (char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z')
                     || (c >= 'a' && c <= 'z')
                     || (c >= '0' && c <= '9')
                     || c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
}

bool AuthService::is_valid_password(std::string_view s) {
    return s.size() >= kMinPassword && s.size() <= kMaxPassword;
}

bool AuthService::is_valid_email(std::string_view s) {
    // Basic sanity: non-empty strings must look email-ish. Empty is allowed
    // elsewhere — this function assumes caller only calls it on non-empty.
    if (s.empty()) return false;
    auto at = s.find('@');
    if (at == std::string_view::npos || at == 0 || at == s.size() - 1) return false;
    auto dot = s.find('.', at);
    if (dot == std::string_view::npos || dot == s.size() - 1) return false;
    return true;
}

std::string AuthService::generate_token() {
    // Crypto-grade entropy via libsodium (randombytes_buf) — CSPRNG seeded
    // from the OS. Returns 32 random bytes hex-encoded (64 chars).
    return crypto::generate_session_token();
}

std::string AuthService::remote_ip_of(net::ConnId conn) const {
    auto s = sessions_.get(conn);
    if (!s) return std::string{};
    return s->remote_addr;
}

void AuthService::send_auth_ok(net::ConnId conn, std::string_view token) {
    flatbuffers::FlatBufferBuilder fbb;
    auto token_off = fbb.CreateString(std::string(token));
    // characters[] is always empty until step 007 introduces the character model.
    std::vector<flatbuffers::Offset<::auth::CharacterSummary>> empty;
    auto chars_off = fbb.CreateVector(empty);
    auto root = ::auth::CreateAuthOk(fbb, token_off, chars_off);
    fbb.Finish(root);

    auto framed = net::encode(net::packet_id("auth.AuthOk"),
                              fbb.GetBufferPointer(),
                              fbb.GetSize());
    sender_(conn, framed.data(), framed.size());
}

void AuthService::send_auth_fail(net::ConnId conn, uint8_t reason) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::auth::CreateAuthFail(fbb, static_cast<::auth::AuthFailReason>(reason));
    fbb.Finish(root);

    auto framed = net::encode(net::packet_id("auth.AuthFail"),
                              fbb.GetBufferPointer(),
                              fbb.GetSize());
    sender_(conn, framed.data(), framed.size());
}

void AuthService::handle_register(net::ConnId conn, const ::auth::Register& pkt) {
    const std::string username = pkt.username() ? pkt.username()->str() : std::string{};
    const std::string password = pkt.password() ? pkt.password()->str() : std::string{};
    const std::string email    = pkt.email()    ? pkt.email()->str()    : std::string{};

    const std::string ip = remote_ip_of(conn);
    const auto now_steady = std::chrono::steady_clock::now();

    if (!rate_limiter_.try_consume_ip(ip, now_steady)) {
        LOG_INF("auth: register rate-limited ip={}", ip);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_RateLimited));
        return;
    }

    if (!is_valid_username(username)) {
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_UsernameInvalid));
        return;
    }
    if (!is_valid_password(password)) {
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_PasswordTooShort));
        return;
    }
    if (!email.empty() && !is_valid_email(email)) {
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_EmailInvalid));
        return;
    }

    if (store_.find_account_by_username(username)) {
        LOG_INF("auth: register username taken: {}", username);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_UsernameTaken));
        return;
    }

    AccountRecord acct;
    try {
        const std::string hashed = crypto::hash_password(password);
        acct = store_.create_account(username, hashed, email);
    } catch (const std::exception& e) {
        if (is_unique_violation(e)) {
            // Lost the race with a concurrent Register for the same username.
            LOG_INF("auth: register race-lost username taken: {}", username);
            send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_UsernameTaken));
            return;
        }
        LOG_ERR("auth: register create_account failed for {}: {}", username, e.what());
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_ServerError));
        return;
    }
    LOG_INF("auth: account created id={} username={}", acct.id, username);

    const std::string token = generate_token();
    const auto now_sys = Clock::now();
    const auto expires = now_sys + cfg_.session_ttl;
    try {
        store_.create_session(acct.id, token, now_sys, expires);
    } catch (const std::exception& e) {
        LOG_ERR("auth: register create_session failed: {}", e.what());
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_ServerError));
        return;
    }

    sessions_.mark_authenticated(conn, acct.id);
    sessions_.set_username(conn, acct.username);
    LOG_INF("auth: register success conn={} account={} token={}",
            conn, acct.id, token_fingerprint(token));

    send_auth_ok(conn, token);
}

void AuthService::handle_login(net::ConnId conn, const ::auth::Login& pkt) {
    const std::string username = pkt.username() ? pkt.username()->str() : std::string{};
    const std::string password = pkt.password() ? pkt.password()->str() : std::string{};

    const std::string ip = remote_ip_of(conn);
    const auto now_steady = std::chrono::steady_clock::now();

    if (!rate_limiter_.try_consume_ip(ip, now_steady)) {
        LOG_INF("auth: login ip-rate-limited ip={}", ip);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_RateLimited));
        return;
    }

    if (rate_limiter_.is_account_locked(username, now_steady)) {
        LOG_INF("auth: login account-locked username={}", username);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_RateLimited));
        return;
    }

    auto acct = store_.find_account_by_username(username);

    // Always run verify_password — even on unknown username — to keep the
    // runtime of this branch indistinguishable from a wrong-password hit.
    // Otherwise an attacker can enumerate usernames by measuring latency.
    const std::string& hash_to_check = acct ? acct->password_hash : dummy_password_hash();
    const bool pw_ok = crypto::verify_password(password, hash_to_check);

    if (!acct || !pw_ok) {
        rate_limiter_.record_failed_login(username, now_steady);
        LOG_INF("auth: login failed username={} reason={}",
                username, acct ? "wrong-password" : "unknown-user");
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_InvalidCredentials));
        return;
    }

    rate_limiter_.clear_failed_logins(username);

    const std::string token = generate_token();
    const auto now_sys = Clock::now();
    const auto expires = now_sys + cfg_.session_ttl;
    try {
        store_.create_session(acct->id, token, now_sys, expires);
    } catch (const std::exception& e) {
        LOG_ERR("auth: login create_session failed: {}", e.what());
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_ServerError));
        return;
    }

    sessions_.mark_authenticated(conn, acct->id);
    sessions_.set_username(conn, acct->username);
    LOG_INF("auth: login success conn={} account={} token={}",
            conn, acct->id, token_fingerprint(token));

    send_auth_ok(conn, token);
}

void AuthService::handle_resume(net::ConnId conn, const ::auth::Resume& pkt) {
    const std::string token = pkt.token() ? pkt.token()->str() : std::string{};

    auto rec = store_.find_session(token);
    if (!rec) {
        LOG_INF("auth: resume unknown-token token={}", token_fingerprint(token));
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_TokenUnknown));
        return;
    }

    const auto now_sys = Clock::now();
    if (rec->expires_at <= now_sys) {
        LOG_INF("auth: resume expired-token token={}", token_fingerprint(token));
        store_.delete_session(token);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_TokenExpired));
        return;
    }

    // Sliding: extend expires_at to now + TTL so an active client keeps
    // its token alive indefinitely. Prompt spec: max(created_at + TTL,
    // last_seen_at + TTL) — since last_seen_at >= created_at, this is
    // simply last_seen_at + TTL.
    store_.touch_session(token, now_sys, now_sys + cfg_.session_ttl);
    sessions_.mark_authenticated(conn, rec->account_id);
    // Resolve the speaker name once so the chat path never hits the DB.
    // A missing account row on Resume shouldn't be fatal — the session still
    // authenticates and chat simply shows an empty display name.
    if (auto acct = store_.find_account_by_id(rec->account_id)) {
        sessions_.set_username(conn, acct->username);
    }
    LOG_INF("auth: resume success conn={} account={} token={}",
            conn, rec->account_id, token_fingerprint(token));

    send_auth_ok(conn, token);
}

} // namespace game::auth
