#include "session/session_manager.h"

namespace game::session {

bool SessionManager::add(net::ConnId id, std::string remote_addr) {
    std::lock_guard<std::mutex> lg(mu_);
    Session s;
    s.conn_id = id;
    s.remote_addr = std::move(remote_addr);
    s.opened_at = std::chrono::steady_clock::now();
    s.state = AuthState::Unauthenticated;
    auto [it, inserted] = sessions_.emplace(id, std::move(s));
    return inserted;
}

void SessionManager::remove(net::ConnId id) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    if (it->second.character_id) {
        conn_by_character_.erase(*it->second.character_id);
    }
    sessions_.erase(it);
}

std::optional<Session> SessionManager::get(net::ConnId id) const {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return std::nullopt;
    return it->second;
}

void SessionManager::mark_authenticated(net::ConnId id, AccountId account_id) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    it->second.state = AuthState::Authenticated;
    it->second.account_id = account_id;
}

void SessionManager::set_username(net::ConnId id, std::string username) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    it->second.username = std::move(username);
}

std::optional<net::ConnId> SessionManager::bind_character(
    net::ConnId id, uint64_t character_id, uint32_t location_id) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return std::nullopt;

    // If this session already had another character bound, drop that
    // mapping first (rare — DeselectCharacter normally clears it).
    if (it->second.character_id) {
        conn_by_character_.erase(*it->second.character_id);
    }

    std::optional<net::ConnId> kicked;
    auto rev_it = conn_by_character_.find(character_id);
    if (rev_it != conn_by_character_.end() && rev_it->second != id) {
        kicked = rev_it->second;
        // Clear the kicked session's bookkeeping. The transport disconnect
        // will follow on the caller's side (we can't disconnect from here —
        // SessionManager has no transport reference).
        if (auto kicked_it = sessions_.find(*kicked); kicked_it != sessions_.end()) {
            kicked_it->second.character_id.reset();
            kicked_it->second.location_id = 0;
        }
    }

    it->second.character_id = character_id;
    it->second.location_id = location_id;
    conn_by_character_[character_id] = id;
    return kicked;
}

void SessionManager::unbind_character(net::ConnId id) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end() || !it->second.character_id) return;
    conn_by_character_.erase(*it->second.character_id);
    it->second.character_id.reset();
    it->second.location_id = 0;
}

void SessionManager::update_character_location(net::ConnId id,
                                               uint32_t new_location_id) {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    it->second.location_id = new_location_id;
}

std::optional<net::ConnId> SessionManager::session_for_character(
    uint64_t character_id) const {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = conn_by_character_.find(character_id);
    if (it == conn_by_character_.end()) return std::nullopt;
    return it->second;
}

std::vector<net::ConnId> SessionManager::authenticated_conn_ids() const {
    std::vector<net::ConnId> out;
    std::lock_guard<std::mutex> lg(mu_);
    out.reserve(sessions_.size());
    for (const auto& [id, s] : sessions_) {
        if (s.state == AuthState::Authenticated) out.push_back(id);
    }
    return out;
}

std::vector<net::ConnId> SessionManager::collect_handshake_timeouts(
    std::chrono::steady_clock::time_point now,
    std::chrono::seconds timeout) const {
    std::vector<net::ConnId> out;
    std::lock_guard<std::mutex> lg(mu_);
    for (const auto& [id, s] : sessions_) {
        if (s.state == AuthState::Unauthenticated && (now - s.opened_at) > timeout) {
            out.push_back(id);
        }
    }
    return out;
}

size_t SessionManager::size() const {
    std::lock_guard<std::mutex> lg(mu_);
    return sessions_.size();
}

} // namespace game::session
