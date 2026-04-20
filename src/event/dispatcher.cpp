#include "event/dispatcher.h"

#include "log/logger.h"

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
                // TODO(step-007): filter to sessions whose character is in
                // `ev.local_location`. Step-006 has no world model, so Local
                // fans out to every Authenticated session — behaviourally
                // identical to Global for now.
                const auto targets = sessions_.authenticated_conn_ids();
                for (auto conn : targets) {
                    sender_(conn, ev.payload.data(), ev.payload.size());
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
