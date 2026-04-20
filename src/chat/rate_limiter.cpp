#include "chat/rate_limiter.h"

#include <algorithm>

namespace game::chat {

RateLimiter::RateLimiter(Config cfg) : cfg_(cfg) {}

void RateLimiter::refill(Bucket& b, const BucketConfig& bc,
                         std::chrono::steady_clock::time_point now) const {
    const auto delta = now - b.last_refill;
    if (delta <= std::chrono::steady_clock::duration::zero()) return;
    const double seconds = std::chrono::duration<double>(delta).count();
    b.tokens = std::min(bc.cap, b.tokens + seconds * bc.refill_per_sec);
    b.last_refill = now;
}

RateLimiter::Bucket& RateLimiter::bucket_for(const Key& key,
                                             std::chrono::steady_clock::time_point now) {
    auto it = buckets_.find(key);
    if (it == buckets_.end()) {
        const auto& bc = config_for(key.channel);
        // New buckets start full — a brand-new account shouldn't be
        // throttled on their first message.
        Bucket fresh{bc.cap, now};
        it = buckets_.emplace(key, fresh).first;
    }
    return it->second;
}

bool RateLimiter::try_consume(session::AccountId account_id, Channel channel,
                              std::chrono::steady_clock::time_point now) {
    const Key key{account_id, channel};
    const auto& bc = config_for(channel);

    std::lock_guard<std::mutex> lg(mu_);
    Bucket& b = bucket_for(key, now);
    refill(b, bc, now);
    if (b.tokens < 1.0) return false;
    b.tokens -= 1.0;
    return true;
}

std::chrono::milliseconds RateLimiter::retry_after(
    session::AccountId account_id, Channel channel,
    std::chrono::steady_clock::time_point now) const {
    const Key key{account_id, channel};
    std::lock_guard<std::mutex> lg(mu_);
    auto it = buckets_.find(key);
    if (it == buckets_.end()) return std::chrono::milliseconds{0};

    const auto& bc = config_for(channel);
    // Recompute the refilled token count without mutating the stored bucket.
    Bucket snapshot = it->second;
    refill(snapshot, bc, now);
    if (snapshot.tokens >= 1.0) return std::chrono::milliseconds{0};
    if (bc.refill_per_sec <= 0.0) {
        // Zero-refill bucket is permanently empty — the client will never
        // recover on its own. Clamp to "try again in a long while" rather
        // than infinity so retry_after_ms fits in a uint32.
        return std::chrono::hours{1};
    }
    const double need = 1.0 - snapshot.tokens;
    const double seconds = need / bc.refill_per_sec;
    // round up so clients don't immediately retry and miss by a microsecond
    return std::chrono::milliseconds{static_cast<int64_t>(seconds * 1000.0 + 0.5)};
}

size_t RateLimiter::tracked_keys() const {
    std::lock_guard<std::mutex> lg(mu_);
    return buckets_.size();
}

} // namespace game::chat
