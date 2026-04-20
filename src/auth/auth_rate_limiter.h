#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game::auth {

// Two independent limiters:
//  - Per-IP token bucket over Register+Login (not Resume): prevents a single
//    IP from flooding the auth endpoints.
//  - Per-account failure counter: prevents brute-forcing a known username
//    from rotating IPs. Locked for 1h once 20 fails land within 1h.
//
// Both are in-memory with std::mutex. Step 005 may move the per-account
// counter to Postgres so it survives restarts.
class AuthRateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    struct Config {
        size_t ip_bucket_cap = 10;
        std::chrono::seconds ip_bucket_refill{6};
        size_t per_account_hourly_fails = 20;
        std::chrono::seconds per_account_lockout{3600};
        std::chrono::seconds per_account_window{3600};
    };

    explicit AuthRateLimiter(Config cfg = {});

    // Try to consume 1 IP token. Returns true if the request may proceed.
    bool try_consume_ip(std::string_view ip, Clock::time_point now);

    // Is this username currently locked out from login attempts?
    bool is_account_locked(std::string_view username, Clock::time_point now) const;

    // Record a failed login for this username. Rolls over a 1h window —
    // fails older than that are forgotten when the next fail lands.
    void record_failed_login(std::string_view username, Clock::time_point now);

    // Clear the failure counter on a successful login.
    void clear_failed_logins(std::string_view username);

private:
    struct IpBucket {
        double tokens;
        Clock::time_point last_refill;
    };

    struct AccountFails {
        size_t count = 0;
        Clock::time_point window_start{};
        Clock::time_point locked_until{};  // == default-constructed when not locked
    };

    static std::string lowercase(std::string_view s);

    Config cfg_;
    mutable std::mutex mu_;
    std::unordered_map<std::string, IpBucket> ip_buckets_;
    std::unordered_map<std::string, AccountFails> account_fails_;
};

} // namespace game::auth
