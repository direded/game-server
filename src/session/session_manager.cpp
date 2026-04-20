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
    sessions_.erase(id);
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
