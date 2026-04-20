#pragma once

#include "net/transport.h"
#include "session/session.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game::session {

// Thread-safe ConnId → Session map. Guarded by one std::mutex — fine at 10k
// connections; revisit sharding if per-session work grows.
class SessionManager {
public:
    // Construct a session on new connection. Returns false if the id is already
    // present (should never happen — the transport assigns unique ids).
    bool add(net::ConnId id, std::string remote_addr);

    // Remove a session on disconnect. No-op if not present.
    void remove(net::ConnId id);

    // Returns a copy of the session so callers don't hold the lock while
    // working with the data. Fine at this scale; revisit if hot path.
    std::optional<Session> get(net::ConnId id) const;

    // Mark a session authenticated and associate it with an account. No-op if
    // the connection has already disconnected.
    void mark_authenticated(net::ConnId id, AccountId account_id);

    // Return conn ids whose session is Unauthenticated and was opened more
    // than `timeout` ago relative to `now`. Used by the transport-side sweep
    // thread to close slow/silent peers; we return ids rather than closing
    // here so SessionManager stays free of transport dependencies.
    std::vector<net::ConnId> collect_handshake_timeouts(
        std::chrono::steady_clock::time_point now,
        std::chrono::seconds timeout) const;

    size_t size() const;

private:
    mutable std::mutex mu_;
    std::unordered_map<net::ConnId, Session> sessions_;
};

} // namespace game::session
