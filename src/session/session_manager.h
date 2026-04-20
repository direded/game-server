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

    // Cache the speaker-display username on the session. Called once after
    // successful auth so the chat path can fan out `ChatMessage` without a
    // DB hit. No-op if the connection has already disconnected.
    void set_username(net::ConnId id, std::string username);

    // Bind a character to a session. If the same character is already
    // attached to a different connection, that connection's id is returned
    // (callers kick it via transport.disconnect — exactly one session may
    // hold a character at a time). Returns std::nullopt if no kick is
    // needed. No-op (returns std::nullopt) if `id` is unknown.
    std::optional<net::ConnId> bind_character(net::ConnId id,
                                              uint64_t character_id,
                                              uint32_t location_id);

    // Detach the current character from this session. No-op if none bound.
    void unbind_character(net::ConnId id);

    // Update the cached location of whichever character is bound to `id`.
    // Called on the sim thread when an action (Travel, ...) moves the
    // character so chat's local routing stays current.
    void update_character_location(net::ConnId id, uint32_t new_location_id);

    // Reverse lookup: which connection currently owns this character?
    std::optional<net::ConnId> session_for_character(uint64_t character_id) const;

    // Snapshot of conn ids for every Authenticated session. Used by the
    // event dispatcher to expand Global / Local scopes at flush time.
    std::vector<net::ConnId> authenticated_conn_ids() const;

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
    // Reverse index for session_for_character — kept in sync with
    // Session::character_id under mu_.
    std::unordered_map<uint64_t, net::ConnId> conn_by_character_;
};

} // namespace game::session
