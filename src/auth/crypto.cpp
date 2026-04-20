#include "auth/crypto.h"

#include <sodium.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace game::auth::crypto {

namespace {

std::once_flag g_init_flag;
bool g_init_ok = false;

void do_init() {
    // sodium_init() returns 0 on success, 1 if already initialized, -1 on
    // failure. 0 and 1 are both fine; -1 is fatal.
    if (sodium_init() < 0) {
        g_init_ok = false;
        return;
    }
    g_init_ok = true;
}

void ensure_init() {
    std::call_once(g_init_flag, &do_init);
    if (!g_init_ok) {
        throw std::runtime_error("libsodium init failed");
    }
}

}  // namespace

void init() {
    ensure_init();
}

std::string hash_password(std::string_view plaintext) {
    ensure_init();

    std::array<char, crypto_pwhash_STRBYTES> out{};
    const int rc = crypto_pwhash_str(
        out.data(),
        plaintext.data(),
        plaintext.size(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE);
    if (rc != 0) {
        throw std::runtime_error("crypto_pwhash_str failed (out of memory?)");
    }
    return std::string(out.data());  // NUL-terminated PHC string
}

bool verify_password(std::string_view plaintext, std::string_view hash) {
    ensure_init();

    // crypto_pwhash_str_verify wants a NUL-terminated C string for the hash.
    // string_view is not guaranteed NUL-terminated, so copy.
    std::string hash_str(hash);
    if (hash_str.size() >= crypto_pwhash_STRBYTES) {
        return false;  // malformed — can't be a valid PHC string
    }
    return crypto_pwhash_str_verify(
        hash_str.c_str(),
        plaintext.data(),
        plaintext.size()) == 0;
}

std::string generate_session_token() {
    ensure_init();

    constexpr size_t kBytes = 32;
    std::array<unsigned char, kBytes> buf{};
    randombytes_buf(buf.data(), buf.size());

    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (unsigned char b : buf) os << std::setw(2) << static_cast<int>(b);
    return os.str();
}

}  // namespace game::auth::crypto
