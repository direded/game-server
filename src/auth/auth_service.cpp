#include "auth/auth_service.h"

#include "log/logger.h"
#include "net/framing.h"
#include "protocol/generated/auth_generated.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <random>
#include <sstream>
#include <iomanip>

namespace game::auth {

namespace {

constexpr size_t kTokenBytes = 32;
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
    // TODO(step-005): replace with libsodium randombytes_buf for crypto-safe
    // entropy. std::mt19937_64 + random_device is adequate as a placeholder
    // but is NOT guaranteed to be cryptographically secure.
    static thread_local std::mt19937_64 rng{std::random_device{}()};

    std::array<uint8_t, kTokenBytes> bytes{};
    for (size_t i = 0; i + 7 < bytes.size(); i += 8) {
        uint64_t v = rng();
        for (size_t j = 0; j < 8; ++j) {
            bytes[i + j] = static_cast<uint8_t>((v >> (j * 8)) & 0xFFu);
        }
    }

    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (uint8_t b : bytes) os << std::setw(2) << static_cast<int>(b);
    return os.str();
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

    // TODO(step-005): replace with Argon2id via libsodium.
    auto acct = store_.create_account(username, password, email);
    LOG_INF("auth: account created id={} username={}", acct.id, username);

    const std::string token = generate_token();
    const auto now_sys = Clock::now();
    const auto expires = now_sys + cfg_.session_ttl;
    store_.create_session(acct.id, token, now_sys, expires);

    sessions_.mark_authenticated(conn, acct.id);
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
    if (!acct) {
        rate_limiter_.record_failed_login(username, now_steady);
        LOG_INF("auth: login unknown-user username={}", username);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_InvalidCredentials));
        return;
    }

    // TODO(step-005): replace with Argon2id via libsodium.
    if (acct->password_hash != password) {
        rate_limiter_.record_failed_login(username, now_steady);
        LOG_INF("auth: login wrong-password username={}", username);
        send_auth_fail(conn, static_cast<uint8_t>(::auth::AuthFailReason_InvalidCredentials));
        return;
    }

    rate_limiter_.clear_failed_logins(username);

    const std::string token = generate_token();
    const auto now_sys = Clock::now();
    const auto expires = now_sys + cfg_.session_ttl;
    store_.create_session(acct->id, token, now_sys, expires);

    sessions_.mark_authenticated(conn, acct->id);
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
    LOG_INF("auth: resume success conn={} account={} token={}",
            conn, rec->account_id, token_fingerprint(token));

    send_auth_ok(conn, token);
}

} // namespace game::auth
