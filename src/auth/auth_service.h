#pragma once

#include "auth/auth_rate_limiter.h"
#include "auth/auth_store.h"
#include "net/transport.h"
#include "session/session_manager.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace game::world { class CharacterStore; }

// Forward-declare the generated FlatBuffers packet types so this header
// doesn't drag in the generated code.
namespace auth {
struct Register;
struct Login;
struct Resume;
} // namespace auth

namespace game::auth {

// Pure-logic handlers for Register / Login / Resume. Depends only on the
// IAuthStore interface and the SessionManager + RateLimiter, so tests can
// swap the transport out for a mock sender.
class AuthService {
public:
    // Sender delivers already-framed bytes (packet_id prefix + flatbuffers
    // payload) to a specific connection. In production this forwards to
    // ITransport::send; tests supply a std::vector capture.
    using Sender = std::function<void(net::ConnId, const uint8_t*, size_t)>;

    struct Config {
        std::chrono::hours session_ttl{24 * 30};  // 30 days
    };

    // `character_store` is optional: nullptr when auth.backend == memory (no DB
    // available). When null, AuthOk.characters is always empty.
    AuthService(IAuthStore& store,
                session::SessionManager& sessions,
                AuthRateLimiter& rate_limiter,
                Sender sender,
                Config cfg,
                world::CharacterStore* character_store = nullptr);

    void handle_register(net::ConnId conn, const ::auth::Register& pkt);
    void handle_login(net::ConnId conn, const ::auth::Login& pkt);
    void handle_resume(net::ConnId conn, const ::auth::Resume& pkt);

    // Exposed for tests. Both are deterministic pure functions.
    static bool is_valid_username(std::string_view s);
    static bool is_valid_password(std::string_view s);
    static bool is_valid_email(std::string_view s);

    // Exposed for tests — generates a 64-char hex token from 32 random bytes.
    // Uses std::random_device; step 005 swaps to libsodium randombytes_buf.
    static std::string generate_token();

private:
    // Encode and dispatch the response packet via sender_. account_id is used
    // to fetch the character list for AuthOk.characters (none if no store).
    void send_auth_ok(net::ConnId conn, std::string_view token, AccountId account_id);
    void send_auth_fail(net::ConnId conn, uint8_t reason);

    std::string remote_ip_of(net::ConnId conn) const;

    IAuthStore& store_;
    session::SessionManager& sessions_;
    AuthRateLimiter& rate_limiter_;
    Sender sender_;
    Config cfg_;
    world::CharacterStore* character_store_ = nullptr;
};

} // namespace game::auth
