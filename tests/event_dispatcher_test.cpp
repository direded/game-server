#include "event/dispatcher.h"
#include "event/event.h"
#include "session/session_manager.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Record every send() the dispatcher fires — per-conn so tests can assert
// routing shape (who got what) rather than just a total count.
struct SendLog {
    std::unordered_map<game::net::ConnId, std::vector<std::vector<uint8_t>>> by_conn;
    size_t total = 0;

    auto as_callback() {
        return [this](game::net::ConnId c, const uint8_t* data, size_t len) {
            by_conn[c].emplace_back(data, data + len);
            ++total;
        };
    }
};

game::event::Event make_event(game::event::EventScope scope,
                              game::net::ConnId target_private = 0) {
    game::event::Event ev;
    ev.scope = scope;
    ev.private_target = target_private;
    ev.packet_id = 0xcafe;
    ev.payload = {0xde, 0xad, 0xbe, 0xef};
    return ev;
}

}  // namespace

TEST(EventDispatcher, PrivateScopeTargetsOnlyNamedConn) {
    game::session::SessionManager sessions;
    sessions.add(1, "a");
    sessions.add(2, "b");
    sessions.add(3, "c");
    sessions.mark_authenticated(1, 100);
    sessions.mark_authenticated(2, 200);
    sessions.mark_authenticated(3, 300);

    SendLog log;
    game::event::EventDispatcher d(sessions, log.as_callback());

    d.emit(make_event(game::event::EventScope::Private, /*target=*/2));
    d.flush();

    EXPECT_EQ(log.total, 1u);
    EXPECT_EQ(log.by_conn[2].size(), 1u);
    EXPECT_EQ(log.by_conn[1].size(), 0u);
    EXPECT_EQ(log.by_conn[3].size(), 0u);
}

TEST(EventDispatcher, GlobalScopeFansOutToAllAuthenticated) {
    game::session::SessionManager sessions;
    sessions.add(1, "a");
    sessions.add(2, "b");
    sessions.add(3, "c");
    sessions.mark_authenticated(1, 100);
    sessions.mark_authenticated(2, 200);
    // Leave conn 3 unauthenticated.

    SendLog log;
    game::event::EventDispatcher d(sessions, log.as_callback());
    d.emit(make_event(game::event::EventScope::Global));
    d.flush();

    EXPECT_EQ(log.total, 2u);
    EXPECT_EQ(log.by_conn[1].size(), 1u);
    EXPECT_EQ(log.by_conn[2].size(), 1u);
    EXPECT_EQ(log.by_conn[3].size(), 0u);  // unauthenticated — excluded
}

TEST(EventDispatcher, LocalScopeBehavesLikeGlobalInStep006) {
    game::session::SessionManager sessions;
    sessions.add(1, "a");
    sessions.add(2, "b");
    sessions.mark_authenticated(1, 100);
    sessions.mark_authenticated(2, 200);

    SendLog log;
    game::event::EventDispatcher d(sessions, log.as_callback());
    d.emit(make_event(game::event::EventScope::Local));
    d.flush();

    // TODO(step-007): once location filtering lands, update this test to
    // assert that only co-located sessions receive Local events.
    EXPECT_EQ(log.total, 2u);
    EXPECT_EQ(log.by_conn[1].size(), 1u);
    EXPECT_EQ(log.by_conn[2].size(), 1u);
}

TEST(EventDispatcher, FlushClearsPendingBuffer) {
    game::session::SessionManager sessions;
    sessions.add(1, "a");
    sessions.mark_authenticated(1, 100);

    SendLog log;
    game::event::EventDispatcher d(sessions, log.as_callback());
    d.emit(make_event(game::event::EventScope::Global));
    EXPECT_EQ(d.pending_size(), 1u);
    d.flush();
    EXPECT_EQ(d.pending_size(), 0u);

    // Second flush with nothing pending sends nothing.
    d.flush();
    EXPECT_EQ(log.total, 1u);
}

TEST(EventDispatcher, PayloadBytesAreForwardedVerbatim) {
    game::session::SessionManager sessions;
    sessions.add(7, "x");
    sessions.mark_authenticated(7, 700);

    SendLog log;
    game::event::EventDispatcher d(sessions, log.as_callback());

    game::event::Event ev;
    ev.scope = game::event::EventScope::Private;
    ev.private_target = 7;
    ev.packet_id = 0x1234;
    ev.payload = {0x01, 0x02, 0x03, 0x04, 0x05};
    d.emit(std::move(ev));
    d.flush();

    ASSERT_EQ(log.by_conn[7].size(), 1u);
    EXPECT_EQ(log.by_conn[7][0], (std::vector<uint8_t>{0x01, 0x02, 0x03, 0x04, 0x05}));
}

TEST(EventDispatcher, EmitIsThreadSafe) {
    // Sanity: concurrent emits from multiple threads don't corrupt the queue.
    game::session::SessionManager sessions;
    sessions.add(1, "a");
    sessions.mark_authenticated(1, 100);

    SendLog log;
    game::event::EventDispatcher d(sessions, log.as_callback());

    constexpr int kThreads = 4;
    constexpr int kPerThread = 50;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]{
            for (int i = 0; i < kPerThread; ++i) {
                d.emit(make_event(game::event::EventScope::Private, 1));
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(d.pending_size(), static_cast<size_t>(kThreads * kPerThread));
    d.flush();
    EXPECT_EQ(log.total, static_cast<size_t>(kThreads * kPerThread));
}
