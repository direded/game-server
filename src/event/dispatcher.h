#pragma once

#include "event/event.h"
#include "net/transport.h"
#include "session/session_manager.h"

#include <functional>
#include <mutex>
#include <vector>

namespace game::world { class World; }

namespace game::event {

// Accumulates per-tick events on the sim thread (and on IO threads for the
// chat path, which bypasses the sim command queue). `flush()` is called by
// the sim loop at end of tick and drains the buffer into the transport.
//
// Thread-safety:
//   - `emit()` is callable from any thread; writes are guarded by a mutex.
//   - `flush()` is expected to be called from the sim thread only, but is
//     also mutex-guarded so tests can drive it from a test thread.
//   - The dispatcher never touches world state — it only knows how to map a
//     routing scope to a set of ConnIds and hand bytes to the sender.
class EventDispatcher {
public:
    using Sender = std::function<void(net::ConnId, const uint8_t*, size_t)>;

    EventDispatcher(session::SessionManager& sessions, Sender sender);

    // Wire the World up after construction (avoids a circular dependency
    // between SimLoop, World, and EventDispatcher at construction time).
    // Without a World, Local-scope events fall back to Global routing —
    // useful in tests that don't care about location filtering.
    void set_world(const game::world::World* w) { world_ = w; }

    // Enqueue an event for the next flush. Safe to call from any thread.
    void emit(Event ev);

    // Drain the pending buffer and ship each event through the sender.
    // Global / Local scopes are expanded against the current set of
    // Authenticated sessions (Local == Global until step-007 adds real
    // locations). Private scope targets only its named ConnId.
    void flush();

    // Exposed only for tests and diagnostics.
    size_t pending_size() const;

private:
    session::SessionManager& sessions_;
    Sender sender_;
    const game::world::World* world_ = nullptr;

    mutable std::mutex mu_;
    std::vector<Event> pending_;
};

} // namespace game::event
