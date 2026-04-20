#include "session/session_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using game::net::ConnId;
using game::session::AuthState;
using game::session::SessionManager;

namespace {
constexpr ConnId kConn1 = 1001;
constexpr ConnId kConn2 = 1002;
}  // namespace

TEST(SessionManager, AddStoresSession) {
    SessionManager m;
    EXPECT_TRUE(m.add(kConn1, "127.0.0.1"));
    auto s = m.get(kConn1);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->conn_id, kConn1);
    EXPECT_EQ(s->remote_addr, "127.0.0.1");
    EXPECT_EQ(s->state, AuthState::Unauthenticated);
    EXPECT_FALSE(s->account_id.has_value());
}

TEST(SessionManager, DoubleAddReturnsFalse) {
    SessionManager m;
    ASSERT_TRUE(m.add(kConn1, "127.0.0.1"));
    EXPECT_FALSE(m.add(kConn1, "10.0.0.1"));

    // Original remote_addr preserved — add() does not overwrite.
    auto s = m.get(kConn1);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->remote_addr, "127.0.0.1");
}

TEST(SessionManager, GetMissingReturnsNullopt) {
    SessionManager m;
    EXPECT_FALSE(m.get(kConn1).has_value());
}

TEST(SessionManager, RemoveClearsSession) {
    SessionManager m;
    m.add(kConn1, "127.0.0.1");
    m.remove(kConn1);
    EXPECT_FALSE(m.get(kConn1).has_value());
}

TEST(SessionManager, RemoveMissingIsNoop) {
    SessionManager m;
    m.remove(kConn1);  // No sessions — should not throw.
    EXPECT_EQ(m.size(), 0u);
}

TEST(SessionManager, MarkAuthenticatedUpdatesStateAndAccount) {
    SessionManager m;
    m.add(kConn1, "127.0.0.1");
    m.mark_authenticated(kConn1, /*account_id=*/42);

    auto s = m.get(kConn1);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->state, AuthState::Authenticated);
    ASSERT_TRUE(s->account_id.has_value());
    EXPECT_EQ(*s->account_id, 42u);
}

TEST(SessionManager, MarkAuthenticatedOnMissingConnIsNoop) {
    SessionManager m;
    m.mark_authenticated(kConn1, 42);  // No session — silently ignored.
    EXPECT_FALSE(m.get(kConn1).has_value());
}

TEST(SessionManager, SizeTracksAddRemove) {
    SessionManager m;
    EXPECT_EQ(m.size(), 0u);
    m.add(kConn1, "a");
    m.add(kConn2, "b");
    EXPECT_EQ(m.size(), 2u);
    m.remove(kConn1);
    EXPECT_EQ(m.size(), 1u);
}

// ─── handshake timeout sweep ──────────────────────────────────────────────

TEST(SessionManager, HandshakeSweepReturnsIdleUnauthenticated) {
    SessionManager m;
    m.add(kConn1, "127.0.0.1");

    // 0-second timeout means "anything already added counts as stale" — avoids
    // a real sleep while still exercising the `now - opened_at > timeout` check.
    auto stale = m.collect_handshake_timeouts(
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1),
        std::chrono::seconds(0));
    ASSERT_EQ(stale.size(), 1u);
    EXPECT_EQ(stale[0], kConn1);
}

TEST(SessionManager, HandshakeSweepIgnoresAuthenticated) {
    SessionManager m;
    m.add(kConn1, "127.0.0.1");
    m.mark_authenticated(kConn1, 7);

    auto stale = m.collect_handshake_timeouts(
        std::chrono::steady_clock::now() + std::chrono::seconds(1),
        std::chrono::seconds(0));
    EXPECT_TRUE(stale.empty());
}

TEST(SessionManager, HandshakeSweepRespectsTimeout) {
    SessionManager m;
    m.add(kConn1, "127.0.0.1");

    // Give the session a generous timeout — it should NOT be collected yet.
    auto stale = m.collect_handshake_timeouts(
        std::chrono::steady_clock::now(), std::chrono::seconds(10));
    EXPECT_TRUE(stale.empty());
}

TEST(SessionManager, HandshakeSweepReturnsMultipleStaleConns) {
    SessionManager m;
    m.add(kConn1, "a");
    m.add(kConn2, "b");
    auto stale = m.collect_handshake_timeouts(
        std::chrono::steady_clock::now() + std::chrono::seconds(1),
        std::chrono::seconds(0));
    EXPECT_EQ(stale.size(), 2u);
}
