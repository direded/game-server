#include "event/dispatcher.h"
#include "event/event.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "protocol/generated/world_generated.h"
#include "session/session_manager.h"
#include "sim/action_resolver.h"
#include "sim/commands/cancel_action.h"
#include "sim/commands/start_action.h"
#include "world/world.h"

#include <gtest/gtest.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr game::world::LocationId kTown = 1;
constexpr game::world::LocationId kForest = 2;
constexpr game::world::LocationId kClearing = 3;

constexpr game::world::CharacterId kCharA = 100;
constexpr game::net::ConnId kConnA = 1;

// Build a Town ↔ Forest ↔ Clearing graph (Town not directly linked to Clearing).
game::world::LocationGraph build_graph() {
    game::world::LocationGraph g;
    g.spawn_id = kTown;
    auto add = [&](game::world::LocationId id, std::string n,
                   std::vector<game::world::LocationLink> links) {
        game::world::Location loc;
        loc.id = id;
        loc.name = std::move(n);
        loc.links = std::move(links);
        g.locations.emplace(id, std::move(loc));
    };
    add(kTown,     "Town",     {{kForest, 8}});
    add(kForest,   "Forest",   {{kTown, 8}, {kClearing, 12}});
    add(kClearing, "Clearing", {{kForest, 12}});
    return g;
}

struct SendLog {
    std::vector<std::pair<game::net::ConnId, std::vector<uint8_t>>> out;
    auto as_callback() {
        return [this](game::net::ConnId c, const uint8_t* data, size_t len) {
            out.emplace_back(c, std::vector<uint8_t>(data, data + len));
        };
    }
    size_t count_packet(uint32_t pid) const {
        size_t n = 0;
        for (const auto& [_, b] : out) {
            auto frame = game::net::decode(b.data(), b.size());
            if (frame && frame->packet_id == pid) ++n;
        }
        return n;
    }
};

struct Fixture {
    game::session::SessionManager sessions;
    game::world::World world;
    SendLog log;
    game::event::EventDispatcher dispatcher;
    game::sim::ActionResolver resolver;

    Fixture()
        : dispatcher(sessions, log.as_callback()),
          resolver(sessions) {
        world.install_locations(build_graph());
        dispatcher.set_world(&world);
    }

    // Add a session + character bound at the given location.
    void add_actor(game::net::ConnId conn, game::world::CharacterId id,
                   game::session::AccountId acct, game::world::LocationId loc,
                   std::string name = "actor") {
        sessions.add(conn, "ip");
        sessions.mark_authenticated(conn, acct);
        sessions.bind_character(conn, id, loc);
        game::world::Character c;
        c.id = id;
        c.account_id = acct;
        c.name = std::move(name);
        c.location_id = loc;
        world.add_character(std::move(c));
        world.place_character_in_location(id, loc);
    }

    // One full sim "tick" pass: resolver advances actions, then flush emitted
    // events. We don't bump world.tick() because the resolver doesn't read it
    // (started_tick is informational).
    void tick() {
        resolver.tick(world, dispatcher);
        dispatcher.flush();
    }
};

}  // namespace

// ── Travel completes at the exact tick (8-tick link) ─────────────────────

TEST(ActionResolver, TravelCompletesAtExactTick) {
    Fixture f;
    f.add_actor(kConnA, kCharA, /*acct=*/100, kTown);

    // Start a Travel command (sim-thread command, runs synchronously here).
    game::sim::CommandContext ctx{f.world, f.dispatcher, /*tick=*/0};
    game::sim::StartActionCommand start(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{kForest}});
    start.execute(ctx);
    f.dispatcher.flush();

    // ActionStarted broadcast to the source location (just the actor here).
    EXPECT_GE(f.log.count_packet(game::net::packet_id("action.ActionStarted")), 1u);

    f.log.out.clear();

    // Travel takes 8 ticks. After 7 it should still be in flight.
    for (int i = 0; i < 7; ++i) f.tick();
    EXPECT_EQ(f.log.count_packet(game::net::packet_id("action.ActionCompleted")), 0u);

    auto* ch_mid = f.world.character(kCharA);
    ASSERT_NE(ch_mid, nullptr);
    EXPECT_EQ(ch_mid->location_id, kTown);
    EXPECT_TRUE(ch_mid->current_action.has_value());

    // 8th tick completes: location flips, action clears, broadcasts emitted.
    f.tick();
    auto* ch = f.world.character(kCharA);
    ASSERT_NE(ch, nullptr);
    EXPECT_EQ(ch->location_id, kForest);
    EXPECT_FALSE(ch->current_action.has_value());

    EXPECT_EQ(f.log.count_packet(game::net::packet_id("action.ActionCompleted")), 1u);
    // CharacterLeft is Local-scoped to the *source* location (Town). The
    // resolver moves the character before emitting, so the source set is
    // empty in the single-actor case → 0 recipients. The multi-actor test
    // covers the case where someone else in Town does hear it.
    EXPECT_EQ(f.log.count_packet(game::net::packet_id("world.CharacterLeft")), 0u);
    // CharacterEntered routes to the destination, which now contains alice.
    EXPECT_EQ(f.log.count_packet(game::net::packet_id("world.CharacterEntered")), 1u);

    // Session.location_id must mirror the character's new home so chat-Local
    // routes correctly on the next packet.
    auto sess = f.sessions.get(kConnA);
    ASSERT_TRUE(sess);
    EXPECT_EQ(sess->location_id, kForest);
}

// ── Cancel mid-travel: no movement, ActionCancelled emitted ──────────────

TEST(ActionResolver, CancelMidTravelLeavesCharacterInPlace) {
    Fixture f;
    f.add_actor(kConnA, kCharA, /*acct=*/100, kTown);

    game::sim::CommandContext ctx{f.world, f.dispatcher, /*tick=*/0};
    game::sim::StartActionCommand start(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{kForest}});
    start.execute(ctx);
    f.dispatcher.flush();
    f.log.out.clear();

    // Advance 3 ticks (still in flight).
    for (int i = 0; i < 3; ++i) f.tick();

    // Cancel.
    game::sim::CancelActionCommand cancel(kConnA, kCharA);
    cancel.execute(ctx);
    f.dispatcher.flush();

    // Character stays in Town and current_action is cleared.
    auto* ch = f.world.character(kCharA);
    ASSERT_NE(ch, nullptr);
    EXPECT_EQ(ch->location_id, kTown);
    EXPECT_FALSE(ch->current_action.has_value());

    EXPECT_GE(f.log.count_packet(game::net::packet_id("action.ActionCancelled")), 1u);
    // No travel-completion side effects.
    EXPECT_EQ(f.log.count_packet(game::net::packet_id("action.ActionCompleted")), 0u);
    EXPECT_EQ(f.log.count_packet(game::net::packet_id("world.CharacterEntered")), 0u);

    // Even if we keep ticking, nothing fires (action is gone).
    f.log.out.clear();
    for (int i = 0; i < 10; ++i) f.tick();
    EXPECT_EQ(f.log.count_packet(game::net::packet_id("action.ActionCompleted")), 0u);
}

// ── Start while already busy → ActionRejected(Busy) ──────────────────────

TEST(ActionResolver, StartWhileBusyRejectsBusy) {
    Fixture f;
    f.add_actor(kConnA, kCharA, /*acct=*/100, kTown);

    game::sim::CommandContext ctx{f.world, f.dispatcher, /*tick=*/0};
    game::sim::StartActionCommand first(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{kForest}});
    first.execute(ctx);
    f.dispatcher.flush();
    f.log.out.clear();

    game::sim::StartActionCommand second(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{kForest}});
    second.execute(ctx);
    f.dispatcher.flush();

    ASSERT_EQ(f.log.count_packet(game::net::packet_id("action.ActionRejected")), 1u);
    // Decode and check reason.
    for (const auto& [conn, bytes] : f.log.out) {
        auto frame = game::net::decode(bytes.data(), bytes.size());
        if (!frame) continue;
        if (frame->packet_id != game::net::packet_id("action.ActionRejected")) continue;
        const auto* msg = flatbuffers::GetRoot<::action::ActionRejected>(frame->payload);
        EXPECT_EQ(msg->reason(), ::action::ActionRejectReason_Busy);
        EXPECT_EQ(conn, kConnA);
    }
}

// ── Start to an unlinked target → ActionRejected(NotLinked) ──────────────

TEST(ActionResolver, StartToUnlinkedRejectsNotLinked) {
    Fixture f;
    f.add_actor(kConnA, kCharA, /*acct=*/100, kTown);  // Town → Clearing has no direct link

    game::sim::CommandContext ctx{f.world, f.dispatcher, /*tick=*/0};
    game::sim::StartActionCommand cmd(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{kClearing}});
    cmd.execute(ctx);
    f.dispatcher.flush();

    ASSERT_EQ(f.log.count_packet(game::net::packet_id("action.ActionRejected")), 1u);
    for (const auto& [conn, bytes] : f.log.out) {
        auto frame = game::net::decode(bytes.data(), bytes.size());
        if (!frame) continue;
        if (frame->packet_id != game::net::packet_id("action.ActionRejected")) continue;
        const auto* msg = flatbuffers::GetRoot<::action::ActionRejected>(frame->payload);
        EXPECT_EQ(msg->reason(), ::action::ActionRejectReason_NotLinked);
    }
    auto* ch = f.world.character(kCharA);
    ASSERT_NE(ch, nullptr);
    EXPECT_FALSE(ch->current_action.has_value());
}

// ── Start to an unknown location → ActionRejected(UnknownLocation) ──────

TEST(ActionResolver, StartToUnknownLocationRejectsUnknownLocation) {
    Fixture f;
    f.add_actor(kConnA, kCharA, /*acct=*/100, kTown);

    game::sim::CommandContext ctx{f.world, f.dispatcher, /*tick=*/0};
    game::sim::StartActionCommand cmd(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{/*nonexistent=*/9999}});
    cmd.execute(ctx);
    f.dispatcher.flush();

    ASSERT_EQ(f.log.count_packet(game::net::packet_id("action.ActionRejected")), 1u);
    for (const auto& [conn, bytes] : f.log.out) {
        auto frame = game::net::decode(bytes.data(), bytes.size());
        if (!frame) continue;
        if (frame->packet_id != game::net::packet_id("action.ActionRejected")) continue;
        const auto* msg = flatbuffers::GetRoot<::action::ActionRejected>(frame->payload);
        EXPECT_EQ(msg->reason(), ::action::ActionRejectReason_UnknownLocation);
    }
}

// ── Travel only emits to occupants of source/destination ─────────────────

TEST(ActionResolver, TravelBroadcastsAreLocalToSourceAndDestination) {
    Fixture f;
    // Actor in Town, observer in Forest (the destination), observer in Town.
    f.add_actor(kConnA, kCharA, /*acct=*/100, kTown, "alice");
    constexpr game::net::ConnId kConnB = 2;
    constexpr game::world::CharacterId kCharB = 200;
    f.add_actor(kConnB, kCharB, /*acct=*/200, kForest, "bob");
    constexpr game::net::ConnId kConnC = 3;
    constexpr game::world::CharacterId kCharC = 300;
    f.add_actor(kConnC, kCharC, /*acct=*/300, kTown, "carol");

    game::sim::CommandContext ctx{f.world, f.dispatcher, /*tick=*/0};
    game::sim::StartActionCommand start(
        kConnA, kCharA, game::world::ActionKind::Travel,
        game::sim::StartActionCommand::Params{game::world::TravelParams{kForest}});
    start.execute(ctx);
    f.dispatcher.flush();
    f.log.out.clear();

    // Drive to completion (8 ticks).
    for (int i = 0; i < 8; ++i) f.tick();

    // Tally per-conn packets.
    std::unordered_map<game::net::ConnId, int> left_count;
    std::unordered_map<game::net::ConnId, int> entered_count;
    std::unordered_map<game::net::ConnId, int> completed_count;
    for (const auto& [conn, bytes] : f.log.out) {
        auto frame = game::net::decode(bytes.data(), bytes.size());
        if (!frame) continue;
        if (frame->packet_id == game::net::packet_id("world.CharacterLeft"))
            ++left_count[conn];
        else if (frame->packet_id == game::net::packet_id("world.CharacterEntered"))
            ++entered_count[conn];
        else if (frame->packet_id == game::net::packet_id("action.ActionCompleted"))
            ++completed_count[conn];
    }

    // CharacterLeft goes to old-location occupants. Alice has already moved
    // to Forest by the time the event fires, so only Carol (still in Town)
    // hears it. Bob, in Forest, does not.
    EXPECT_EQ(left_count[kConnA], 0);
    EXPECT_EQ(left_count[kConnB], 0);
    EXPECT_EQ(left_count[kConnC], 1);

    // CharacterEntered is to the new-location occupants. Alice (now in Forest)
    // and Bob both receive it; Carol does not.
    EXPECT_EQ(entered_count[kConnA], 1);
    EXPECT_EQ(entered_count[kConnB], 1);
    EXPECT_EQ(entered_count[kConnC], 0);

    // ActionCompleted is private to the actor only.
    EXPECT_EQ(completed_count[kConnA], 1);
    EXPECT_EQ(completed_count[kConnB], 0);
    EXPECT_EQ(completed_count[kConnC], 0);
}
