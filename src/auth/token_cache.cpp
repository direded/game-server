#include "auth/token_cache.h"

#include "log/logger.h"

#include <utility>

namespace game::auth {

TokenCache::TokenCache(IAuthStore& underlying, Config cfg)
    : underlying_(underlying), cfg_(cfg) {}

Clock::time_point TokenCache::now() const {
    return now_fn_ ? now_fn_() : Clock::now();
}

void TokenCache::set_now_for_test(std::function<Clock::time_point()> now_fn) {
    std::lock_guard<std::mutex> lg(mu_);
    now_fn_ = std::move(now_fn);
}

std::optional<AccountRecord> TokenCache::find_account_by_username(
    std::string_view username) {
    return underlying_.find_account_by_username(username);
}

std::optional<AccountRecord> TokenCache::find_account_by_id(AccountId id) {
    return underlying_.find_account_by_id(id);
}

AccountRecord TokenCache::create_account(std::string_view username,
                                         std::string_view password_hash,
                                         std::string_view email) {
    return underlying_.create_account(username, password_hash, email);
}

SessionRecord TokenCache::create_session(AccountId account_id,
                                         std::string token,
                                         Clock::time_point created_at,
                                         Clock::time_point expires_at) {
    auto rec = underlying_.create_session(account_id, token, created_at, expires_at);

    Entry entry;
    entry.account_id = rec.account_id;
    entry.created_at = rec.created_at;
    entry.expires_at = rec.expires_at;
    entry.last_seen_at = rec.last_seen_at;
    {
        std::lock_guard<std::mutex> lg(mu_);
        entry.cached_at = now();
        entry.last_flushed_at = entry.cached_at;
        by_token_.insert_or_assign(rec.token, entry);
    }
    return rec;
}

std::optional<SessionRecord> TokenCache::find_session(std::string_view token) {
    const std::string key(token);
    const auto t_now = now();

    {
        std::lock_guard<std::mutex> lg(mu_);
        auto it = by_token_.find(key);
        if (it != by_token_.end() && t_now - it->second.cached_at < cfg_.cache_ttl) {
            const Entry& e = it->second;
            LOG_DBG("auth: token cache HIT token={}...", key.substr(0, 8));
            SessionRecord out;
            out.token = key;
            out.account_id = e.account_id;
            out.created_at = e.created_at;
            out.expires_at = e.expires_at;
            out.last_seen_at = e.last_seen_at;
            return out;
        }
    }

    LOG_DBG("auth: token cache MISS token={}...", key.substr(0, 8));
    auto rec_opt = underlying_.find_session(token);
    if (!rec_opt) return std::nullopt;

    Entry entry;
    entry.account_id = rec_opt->account_id;
    entry.created_at = rec_opt->created_at;
    entry.expires_at = rec_opt->expires_at;
    entry.last_seen_at = rec_opt->last_seen_at;
    entry.cached_at = t_now;
    entry.last_flushed_at = t_now;
    {
        std::lock_guard<std::mutex> lg(mu_);
        by_token_.insert_or_assign(key, entry);
    }
    return rec_opt;
}

void TokenCache::touch_session(std::string_view token,
                               Clock::time_point last_seen_at,
                               Clock::time_point expires_at) {
    const std::string key(token);
    bool needs_flush = false;

    {
        std::lock_guard<std::mutex> lg(mu_);
        auto it = by_token_.find(key);
        if (it == by_token_.end()) {
            // Nothing cached — write through so the store is always correct.
            needs_flush = true;
        } else {
            it->second.last_seen_at = last_seen_at;
            it->second.expires_at = expires_at;
            const auto since_flush = last_seen_at - it->second.last_flushed_at;
            if (since_flush >= cfg_.touch_interval) {
                needs_flush = true;
                it->second.last_flushed_at = last_seen_at;
            }
        }
    }

    if (needs_flush) {
        underlying_.touch_session(token, last_seen_at, expires_at);
    }
}

void TokenCache::delete_session(std::string_view token) {
    const std::string key(token);
    {
        std::lock_guard<std::mutex> lg(mu_);
        by_token_.erase(key);
    }
    underlying_.delete_session(token);
}

void TokenCache::delete_sessions_for_account(AccountId account_id) {
    {
        std::lock_guard<std::mutex> lg(mu_);
        for (auto it = by_token_.begin(); it != by_token_.end();) {
            if (it->second.account_id == account_id) {
                it = by_token_.erase(it);
            } else {
                ++it;
            }
        }
    }
    underlying_.delete_sessions_for_account(account_id);
}

void TokenCache::invalidate(std::string_view token) {
    std::lock_guard<std::mutex> lg(mu_);
    by_token_.erase(std::string(token));
}

void TokenCache::invalidate_for_account(AccountId account_id) {
    std::lock_guard<std::mutex> lg(mu_);
    for (auto it = by_token_.begin(); it != by_token_.end();) {
        if (it->second.account_id == account_id) {
            it = by_token_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t TokenCache::cache_size() const {
    std::lock_guard<std::mutex> lg(mu_);
    return by_token_.size();
}

}  // namespace game::auth
