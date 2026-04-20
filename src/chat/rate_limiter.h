#pragma once

#include "session/session.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace game::chat {

// Channel enum local to the rate limiter so this header doesn't depend on
// the generated FlatBuffers code. The values must line up 1:1 with
// `chat::ChatChannel` in protocol/chat.fbs — the chat service maps between
// them at the boundary.
enum class Channel : uint8_t {
    Local  = 0,
    Global = 1,
};

// Per-(account, channel) token bucket. Refill is lazy: no periodic sweep,
// every try_consume() computes tokens gained since the last check from the
// wall clock.
//
// Config values:
//   cap           — maximum tokens the bucket holds
//   refill_per_sec — tokens added per second of wall clock; can be < 1.0
//                   (e.g. 0.2 = one token every 5 seconds for Global chat)
class RateLimiter {
public:
    struct BucketConfig {
        double cap = 5.0;
        double refill_per_sec = 1.0;
    };

    struct Config {
        BucketConfig local;
        BucketConfig global;
    };

    explicit RateLimiter(Config cfg);

    // Attempt to consume one token. Returns true if the bucket had >=1 and
    // was decremented; false if empty (and by extension, the caller should
    // reply with ChatThrottled).
    bool try_consume(session::AccountId account_id, Channel channel,
                     std::chrono::steady_clock::time_point now);

    // How long until the bucket has >=1 token again. Zero if the bucket is
    // already refillable right now. For ChatThrottled.retry_after_ms.
    std::chrono::milliseconds retry_after(session::AccountId account_id, Channel channel,
                                          std::chrono::steady_clock::time_point now) const;

    // Number of distinct (account, channel) pairs currently tracked. Test hook.
    size_t tracked_keys() const;

private:
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };
    struct Key {
        session::AccountId account_id;
        Channel channel;
        bool operator==(const Key& o) const noexcept {
            return account_id == o.account_id && channel == o.channel;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            // Channel is 0/1 — fold into the low bit of a hashed account id.
            // Collision rate doesn't matter here (two buckets per account
            // at most).
            return std::hash<uint64_t>{}(k.account_id ^ (static_cast<uint64_t>(k.channel) << 62));
        }
    };

    const BucketConfig& config_for(Channel c) const {
        return c == Channel::Global ? cfg_.global : cfg_.local;
    }
    Bucket& bucket_for(const Key& key, std::chrono::steady_clock::time_point now);
    void refill(Bucket& b, const BucketConfig& bc,
                std::chrono::steady_clock::time_point now) const;

    Config cfg_;
    mutable std::mutex mu_;
    std::unordered_map<Key, Bucket, KeyHash> buckets_;
};

} // namespace game::chat
