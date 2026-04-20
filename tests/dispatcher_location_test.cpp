// Verifies that EventDispatcher's `Local` scope only reaches sessions whose
// bound character currently sits in the named location. Also covers the
// "no World wired" fallback path (legacy Global-style fan-out used by some
// of the step-006 tests).

#include "event/dispatcher.h"
#include "event/event.h"
#include "session/session_manager.h"
#include "world/character.h"
#include "world/locations_loader.h"
#include "world/world.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace {

// Captures (conn, payload-byte-zero) pairs so tests can both count sends and
// confirm which conn(s) received them.
struct SendLog {
    std::vector<std::pair<game::net::ConnId, uint8_t>> out;
    auto as_callback() {
        return [this](game::net::ConnId c, const uint8_t* data, size_t /*len*/) {
            out.emplace_back(c, data[0]);
        };
    }
    bool received(game::net::ConnId c) const {
        for (const auto& p : out) if (p.first == c) return true;
        return false;
    }
    size_t count_for(game::net::ConnId c) const {
        size_t n = 0;
        for (const auto& p : out) if (p.first == c) ++n;
        return n;
    }
};

// Two-location graph: Town(1) ↔ Forest(2). Built directly (no YAML) to keep
// the test independent of the loader.
game::world::LocationGraph two_location_graph() {
    using namespace game::world;
    LocationGraph g;
    g.spawn_id = 1;
    Location town;
    town.id = 1; town.name = "Town";
    town.links.push_back({2, 4});
    Location forest;
    forest.id = 2; forest.name = "Forest";
    forest.links.push_back({1, 4});
    g.locations.emplace(1, std::move(town));
    g.locations.emplace(2, std::move(forest));
    return g;
}

// Build a single-byte payload (its first byte is the "tag" tests assert on).
std::vector<uint8_t> tag(uint8_t v) { return {v}; }

}  // namespace

// ── With a World wired: Local routes by location ───────────────────────────

TEST(DispatcherLocation, LocalRoutesOnlyToCharactersInThatLocation) {
    using namespace game::world;
    using namespace game::session;

    World world;
    world.install_locations(two_location_graph());

    // Three characters: alice + carol in Town, bob in Forest.
    Character alice; alice.id = 100; alice.account_id = 1; alice.name = "alice"; alice.location_id = 1;
    Character bob;   bob.id   = 200; bob.account_id   = 2; bob.name   = "bob";   bob.location_id   = 2;
    Character carol; carol.id = 300; carol.account_id = 3; carol.name = "carol"; carol.location_id = 1;
    world.install_characters({alice, bob, carol});
    world.place_character_in_location(100, 1);
    world.place_character_in_location(200, 2);
    world.place_character_in_location(300, 1);

    SessionManager sessions;
    sessions.add(/*conn=*/10, "ip-a"); sessions.mark_authenticated(10, 1); sessions.bind_character(10, 100, 1);
    sessions.add(/*conn=*/20, "ip-b"); sessions.mark_authenticated(20, 2); sessions.bind_character(20, 200, 2);
    sessions.add(/*conn=*/30, "ip-c"); sessions.mark_authenticated(30, 3); sessions.bind_character(30, 300, 1);

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    disp.set_world(&world);

    // Local event in Town (1) — should reach alice + carol, NOT bob.
    game::event::Event ev;
    ev.scope = game::event::EventScope::Local;
    ev.local_location = 1;
    ev.packet_id = 0xAA;
    ev.payload = tag(0xAA);
    disp.emit(std::move(ev));
    disp.flush();

    EXPECT_EQ(log.out.size(), 2u);
    EXPECT_TRUE(log.received(10));
    EXPECT_TRUE(log.received(30));
    EXPECT_FALSE(log.received(20));
}

TEST(DispatcherLocation, LocalSkipsAuthenticatedSessionsWithoutACharacter) {
    using namespace game::world;
    using namespace game::session;

    World world;
    world.install_locations(two_location_graph());

    Character alice; alice.id = 100; alice.account_id = 1; alice.name = "alice"; alice.location_id = 1;
    world.install_characters({alice});
    world.place_character_in_location(100, 1);

    SessionManager sessions;
    sessions.add(10, "ip-a"); sessions.mark_authenticated(10, 1); sessions.bind_character(10, 100, 1);
    // conn 20 is authenticated but has no character — must NOT receive a
    // Local event (Global would reach it, Local should not).
    sessions.add(20, "ip-b"); sessions.mark_authenticated(20, 2);

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    disp.set_world(&world);

    game::event::Event ev;
    ev.scope = game::event::EventScope::Local;
    ev.local_location = 1;
    ev.payload = tag(0xBB);
    disp.emit(std::move(ev));
    disp.flush();

    EXPECT_EQ(log.out.size(), 1u);
    EXPECT_TRUE(log.received(10));
    EXPECT_FALSE(log.received(20));
}

TEST(DispatcherLocation, LocalToUnknownLocationDropsTheEvent) {
    using namespace game::world;
    using namespace game::session;

    World world;
    world.install_locations(two_location_graph());

    Character alice; alice.id = 100; alice.account_id = 1; alice.name = "alice"; alice.location_id = 1;
    world.install_characters({alice});
    world.place_character_in_location(100, 1);

    SessionManager sessions;
    sessions.add(10, "ip-a"); sessions.mark_authenticated(10, 1); sessions.bind_character(10, 100, 1);

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    disp.set_world(&world);

    game::event::Event ev;
    ev.scope = game::event::EventScope::Local;
    ev.local_location = 999;  // not in the graph
    ev.payload = tag(0xCC);
    disp.emit(std::move(ev));
    disp.flush();

    EXPECT_TRUE(log.out.empty());
}

TEST(DispatcherLocation, GlobalReachesEveryAuthenticatedSession) {
    using namespace game::world;
    using namespace game::session;

    World world;
    world.install_locations(two_location_graph());

    Character alice; alice.id = 100; alice.account_id = 1; alice.name = "alice"; alice.location_id = 1;
    Character bob;   bob.id   = 200; bob.account_id   = 2; bob.name   = "bob";   bob.location_id   = 2;
    world.install_characters({alice, bob});
    world.place_character_in_location(100, 1);
    world.place_character_in_location(200, 2);

    SessionManager sessions;
    sessions.add(10, "ip-a"); sessions.mark_authenticated(10, 1); sessions.bind_character(10, 100, 1);
    sessions.add(20, "ip-b"); sessions.mark_authenticated(20, 2); sessions.bind_character(20, 200, 2);
    // Authenticated, no character — Global still reaches it.
    sessions.add(30, "ip-c"); sessions.mark_authenticated(30, 3);
    // Unauthenticated — Global must skip.
    sessions.add(40, "ip-d");

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    disp.set_world(&world);

    game::event::Event ev;
    ev.scope = game::event::EventScope::Global;
    ev.payload = tag(0xDD);
    disp.emit(std::move(ev));
    disp.flush();

    EXPECT_EQ(log.out.size(), 3u);
    EXPECT_TRUE(log.received(10));
    EXPECT_TRUE(log.received(20));
    EXPECT_TRUE(log.received(30));
    EXPECT_FALSE(log.received(40));
}

TEST(DispatcherLocation, MovingACharacterChangesLocalRouting) {
    using namespace game::world;
    using namespace game::session;

    World world;
    world.install_locations(two_location_graph());

    Character alice; alice.id = 100; alice.account_id = 1; alice.name = "alice"; alice.location_id = 1;
    Character bob;   bob.id   = 200; bob.account_id   = 2; bob.name   = "bob";   bob.location_id   = 2;
    world.install_characters({alice, bob});
    world.place_character_in_location(100, 1);
    world.place_character_in_location(200, 2);

    SessionManager sessions;
    sessions.add(10, "ip-a"); sessions.mark_authenticated(10, 1); sessions.bind_character(10, 100, 1);
    sessions.add(20, "ip-b"); sessions.mark_authenticated(20, 2); sessions.bind_character(20, 200, 2);

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    disp.set_world(&world);

    // Pre-move: Local in Forest only reaches bob.
    {
        game::event::Event ev;
        ev.scope = game::event::EventScope::Local;
        ev.local_location = 2;
        ev.payload = tag(0xE0);
        disp.emit(std::move(ev));
        disp.flush();
    }
    EXPECT_EQ(log.count_for(10), 0u);
    EXPECT_EQ(log.count_for(20), 1u);

    // Move alice to the Forest at the world level + mirror to her session.
    ASSERT_TRUE(world.move_character(100, 2));
    sessions.update_character_location(10, 2);

    // Post-move: Local in Forest now reaches both.
    {
        game::event::Event ev;
        ev.scope = game::event::EventScope::Local;
        ev.local_location = 2;
        ev.payload = tag(0xE1);
        disp.emit(std::move(ev));
        disp.flush();
    }
    EXPECT_EQ(log.count_for(10), 1u);
    EXPECT_EQ(log.count_for(20), 2u);

    // And Town is now empty — Local in Town reaches nobody.
    log.out.clear();
    {
        game::event::Event ev;
        ev.scope = game::event::EventScope::Local;
        ev.local_location = 1;
        ev.payload = tag(0xE2);
        disp.emit(std::move(ev));
        disp.flush();
    }
    EXPECT_TRUE(log.out.empty());
}

TEST(DispatcherLocation, PrivateAlwaysTargetsTheNamedConnIgnoringLocation) {
    using namespace game::world;
    using namespace game::session;

    World world;
    world.install_locations(two_location_graph());

    Character alice; alice.id = 100; alice.account_id = 1; alice.name = "alice"; alice.location_id = 1;
    world.install_characters({alice});
    world.place_character_in_location(100, 1);

    SessionManager sessions;
    sessions.add(10, "ip-a"); sessions.mark_authenticated(10, 1); sessions.bind_character(10, 100, 1);
    sessions.add(20, "ip-b"); sessions.mark_authenticated(20, 2);  // no character

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    disp.set_world(&world);

    // Private to conn 20 — must reach 20 even though it's character-less.
    game::event::Event ev;
    ev.scope = game::event::EventScope::Private;
    ev.private_target = 20;
    ev.payload = tag(0xF0);
    disp.emit(std::move(ev));
    disp.flush();

    ASSERT_EQ(log.out.size(), 1u);
    EXPECT_EQ(log.out[0].first, 20u);
}

// ── Without a World wired: Local falls back to Global fan-out ──────────────
//
// Documents the legacy compatibility path used by step-006 tests so a future
// refactor doesn't quietly drop it.

TEST(DispatcherLocation, NoWorldWired_LocalFallsBackToGlobalFanout) {
    using namespace game::session;

    SessionManager sessions;
    sessions.add(10, "ip-a"); sessions.mark_authenticated(10, 1);
    sessions.add(20, "ip-b"); sessions.mark_authenticated(20, 2);
    sessions.add(30, "ip-c");  // unauthenticated — must not receive

    SendLog log;
    game::event::EventDispatcher disp(sessions, log.as_callback());
    // Note: set_world() not called.

    game::event::Event ev;
    ev.scope = game::event::EventScope::Local;
    ev.local_location = 1;
    ev.payload = tag(0xAB);
    disp.emit(std::move(ev));
    disp.flush();

    EXPECT_EQ(log.out.size(), 2u);
    EXPECT_TRUE(log.received(10));
    EXPECT_TRUE(log.received(20));
    EXPECT_FALSE(log.received(30));
}
