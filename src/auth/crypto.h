#pragma once

#include <string>
#include <string_view>

// libsodium-backed primitives: Argon2id password hashing + session tokens.
// init() must be called once at process start (main + test-process boot)
// before any other function in this namespace — sodium_init() is not
// optional. init() is safe to call from multiple threads and idempotent.
namespace game::auth::crypto {

void init();

// Argon2id (INTERACTIVE profile — ops=2, mem=64MiB, matches OWASP 2024 for
// interactive login paths). Output includes the full PHC string (algorithm,
// params, salt, hash) — typically 90+ bytes. Throws std::runtime_error if
// libsodium reports failure (e.g., memory-allocation failure on pathological
// hardware).
std::string hash_password(std::string_view plaintext);

// Constant-time verify of a PHC hash produced by hash_password() (or by any
// other Argon2id implementation producing the same string format). Returns
// false for mismatch OR for malformed hash strings.
bool verify_password(std::string_view plaintext, std::string_view hash);

// 32 random bytes from randombytes_buf, hex-encoded (64 chars).
std::string generate_session_token();

}  // namespace game::auth::crypto
