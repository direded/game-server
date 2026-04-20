#include "event/dispatcher.h"

#include "log/logger.h"
#include "world/location.h"
#include "world/world.h"

namespace game::event {

EventDispatcher::EventDispatcher(session::SessionManager& sessions, Sender sender)
    : sessions_(sessions), sender_(std::move(sender)) {}

void EventDispatcher::emit(Event ev) {
    std::lock_guard<std::mutex> lg(mu_);
    pending_.push_back(std::move(ev));
}

void EventDispatcher::flush() {
    // Swap the pending buffer out under the lock so the send path doesn't
    // hold mu_ while it fans out to potentially thousands of conns (would
    // serialize every emit() behind each flush).
    std::vector<Event> batch;
    {
        std::lock_guard<std::mutex> lg(mu_);
        batch.swap(pending_);
    }
    if (batch.empty()) return;

    for (const auto& ev : batch) {
        switch (ev.scope) {
            case EventScope::Private: {
                sender_(ev.private_target, ev.payload.data(), ev.payload.size());
                break;
            }
            case EventScope::Global: {
                const auto targets = sessions_.authenticated_conn_ids();
                for (auto conn : targets) {
                    sender_(conn, ev.payload.data(), ev.payload.size());
                }
                break;
            }
            case EventScope::Local: {
                // Resolve to the set of characters currently in the named
                // location; for each, look up the bound session and send.
                // Falls back to Global fan-out if no World is wired (used by
                // legacy tests that predate step-007).
                if (world_ == nullptr) {
                    const auto targets = sessions_.authenticated_conn_ids();
                    for (auto conn : targets) {
                        sender_(conn, ev.payload.data(), ev.payload.size());
                    }
                    break;
                }
                const auto* loc = world_->location(ev.local_location);
                if (loc == nullptr) {
                    // Unknown location id on a Local event is a programming
                    // bug, not a hostile-peer concern — log it loudly so it
                    // shows up in tests, then drop the event.
                    LOG_WRN("event: Local event for unknown location {}",
                            ev.local_location);
                    break;
                }
                for (auto char_id : loc->characters) {
                    if (auto conn = sessions_.session_for_character(char_id)) {
                        sender_(*conn, ev.payload.data(), ev.payload.size());
                    }
                }
                break;
            }
        }
    }

    LOG_DBG("event: flushed {} event(s)", batch.size());
}

size_t EventDispatcher::pending_size() const {
    std::lock_guard<std::mutex> lg(mu_);
    return pending_.size();
}

} // namespace game::event
