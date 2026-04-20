#include "auth/auth_rate_limiter.h"

#include <algorithm>
#include <cctype>

namespace game::auth {

std::string AuthRateLimiter::lowercase(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

AuthRateLimiter::AuthRateLimiter(Config cfg) : cfg_(cfg) {}

bool AuthRateLimiter::try_consume_ip(std::string_view ip, Clock::time_point now) {
    const std::string key(ip);
    std::lock_guard<std::mutex> lg(mu_);

    auto it = ip_buckets_.find(key);
    if (it == ip_buckets_.end()) {
        // First request from this IP. Seed with full bucket, then consume one.
        ip_buckets_.emplace(key, IpBucket{static_cast<double>(cfg_.ip_bucket_cap) - 1.0, now});
        return true;
    }

    IpBucket& b = it->second;
    const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - b.last_refill).count();
    const double refill_rate = 1.0 / static_cast<double>(cfg_.ip_bucket_refill.count());
    b.tokens = std::min(static_cast<double>(cfg_.ip_bucket_cap),
                        b.tokens + elapsed * refill_rate);
    b.last_refill = now;

    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return true;
    }
    return false;
}

bool AuthRateLimiter::is_account_locked(std::string_view username, Clock::time_point now) const {
    const std::string key = lowercase(username);
    std::lock_guard<std::mutex> lg(mu_);
    auto it = account_fails_.find(key);
    if (it == account_fails_.end()) return false;
    return it->second.locked_until > now;
}

void AuthRateLimiter::record_failed_login(std::string_view username, Clock::time_point now) {
    const std::string key = lowercase(username);
    std::lock_guard<std::mutex> lg(mu_);
    auto& f = account_fails_[key];

    if (f.locked_until > now) {
        // Already locked — don't extend the lockout by piling on fails.
        return;
    }

    // Outside the rolling window → reset.
    if (f.count == 0 || (now - f.window_start) > cfg_.per_account_window) {
        f.count = 1;
        f.window_start = now;
        return;
    }

    ++f.count;
    if (f.count >= cfg_.per_account_hourly_fails) {
        f.locked_until = now + cfg_.per_account_lockout;
    }
}

void AuthRateLimiter::clear_failed_logins(std::string_view username) {
    const std::string key = lowercase(username);
    std::lock_guard<std::mutex> lg(mu_);
    account_fails_.erase(key);
}

} // namespace game::auth
