#include "auth/in_memory_auth_store.h"

#include <algorithm>
#include <cctype>

namespace game::auth {

std::string InMemoryAuthStore::to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::optional<AccountRecord> InMemoryAuthStore::find_account_by_username(std::string_view username) {
    const std::string key = to_lower(username);
    std::lock_guard<std::mutex> lg(mu_);
    auto it = accounts_by_username_.find(key);
    if (it == accounts_by_username_.end()) return std::nullopt;
    return it->second;
}

AccountRecord InMemoryAuthStore::create_account(std::string_view username,
                                                std::string_view password_hash,
                                                std::string_view email) {
    AccountRecord rec;
    rec.id = next_account_id_.fetch_add(1, std::memory_order_relaxed);
    rec.username = std::string(username);
    // TODO(step-005): replace with Argon2id via libsodium.
    rec.password_hash = std::string(password_hash);
    rec.email = std::string(email);

    const std::string key = to_lower(username);
    std::lock_guard<std::mutex> lg(mu_);
    accounts_by_username_.emplace(key, rec);
    return rec;
}

SessionRecord InMemoryAuthStore::create_session(AccountId account_id,
                                                std::string token,
                                                Clock::time_point created_at,
                                                Clock::time_point expires_at) {
    SessionRecord rec;
    rec.token = std::move(token);
    rec.account_id = account_id;
    rec.created_at = created_at;
    rec.expires_at = expires_at;
    rec.last_seen_at = created_at;

    std::lock_guard<std::mutex> lg(mu_);
    sessions_by_token_.emplace(rec.token, rec);
    return rec;
}

std::optional<SessionRecord> InMemoryAuthStore::find_session(std::string_view token) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_by_token_.find(std::string(token));
    if (it == sessions_by_token_.end()) return std::nullopt;
    return it->second;
}

void InMemoryAuthStore::touch_session(std::string_view token,
                                      Clock::time_point last_seen_at,
                                      Clock::time_point expires_at) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_by_token_.find(std::string(token));
    if (it == sessions_by_token_.end()) return;
    it->second.last_seen_at = last_seen_at;
    it->second.expires_at = expires_at;
}

void InMemoryAuthStore::delete_session(std::string_view token) {
    std::lock_guard<std::mutex> lg(mu_);
    sessions_by_token_.erase(std::string(token));
}

void InMemoryAuthStore::delete_sessions_for_account(AccountId account_id) {
    std::lock_guard<std::mutex> lg(mu_);
    for (auto it = sessions_by_token_.begin(); it != sessions_by_token_.end();) {
        if (it->second.account_id == account_id) {
            it = sessions_by_token_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t InMemoryAuthStore::account_count() const {
    std::lock_guard<std::mutex> lg(mu_);
    return accounts_by_username_.size();
}

size_t InMemoryAuthStore::session_count() const {
    std::lock_guard<std::mutex> lg(mu_);
    return sessions_by_token_.size();
}

} // namespace game::auth
