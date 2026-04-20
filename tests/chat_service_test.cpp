#include "chat/chat_service.h"
#include "chat/rate_limiter.h"
#include "event/dispatcher.h"
#include "net/framing.h"
#include "protocol/generated/chat_generated.h"
#include "session/session_manager.h"
#include "sim/sim_loop.h"
#include "sim/world.h"

#include <flatbuffers/flatbuffers.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

struct SendLog {
    std::vector<std::pair<game::net::ConnId, std::vector<uint8_t>>> out;
    auto as_callback() {
        return [this](game::net::ConnId c, const uint8_t* data, size_t len) {
            out.emplace_back(c, std::vector<uint8_t>(data, data + len));
        };
    }
};

std::vector<uint8_t> build_chat_say(::chat::ChatChannel channel, std::string_view text) {
    flatbuffers::FlatBufferBuilder fbb;
    auto text_off = fbb.CreateString(std::string(text));
    auto root = ::chat::CreateChatSay(fbb, channel, text_off);
    fbb.Finish(root);
    return std::vector<uint8_t>(fbb.GetBufferPointer(),
                                fbb.GetBufferPointer() + fbb.GetSize());
}

const ::chat::ChatSay& parse_chat_say(const std::vector<uint8_t>& bytes) {
    return *flatbuffers::GetRoot<::chat::ChatSay>(bytes.data());
}

// A harness that wires the chat service + its downstream dispatcher.
struct Fixture {
    game::session::SessionManager sessions;
    game::sim::World world;
    // EventDispatcher for this fixture mirrors each event directly into
    // `dispatch_log` as a (scope, conn)-shaped record so tests assert routing.
    SendLog dispatch_log;
    game::event::EventDispatcher dispatcher;
    game::sim::SimLoop sim_loop;
    game::chat::RateLimiter::Config rl_cfg;
    game::chat::RateLimiter rate_limiter;
    SendLog private_log;
    game::chat::ChatService service;

    Fixture()
        : dispatcher(sessions, dispatch_log.as_callback()),
          sim_loop(world, dispatcher, std::chrono::milliseconds(1000)),
          rl_cfg{{5.0, 1.0}, {3.0, 0.2}},
          rate_limiter(rl_cfg),
          service(sessions, rate_limiter, dispatcher, sim_loop,
                  private_log.as_callback(), {500}) {}

    void add_authed(game::net::ConnId c, game::session::AccountId a, std::string name) {
        sessions.add(c, "test-ip");
        sessions.mark_authenticated(c, a);
        sessions.set_username(c, std::move(name));
    }

    void deliver(game::net::ConnId c, const std::vector<uint8_t>& raw_say) {
        service.handle_chat_say(c, parse_chat_say(raw_say));
    }
};

}  // namespace

// ── validation ────────────────────────────────────────────────────────────

TEST(ChatService, IsValidText_AcceptsShortText) {
    const std::string s = "hi";
    EXPECT_TRUE(game::chat::ChatService::is_valid_text(s.data(), s.size(), 500));
}

TEST(ChatService, IsValidText_RejectsEmpty) {
    EXPECT_FALSE(game::chat::ChatService::is_valid_text(nullptr, 0, 500));
    const std::string empty = "";
    EXPECT_FALSE(game::chat::ChatService::is_valid_text(empty.data(), empty.size(), 500));
}

TEST(ChatService, IsValidText_RejectsAllWhitespace) {
    const std::string blank = "   \t  \n";
    EXPECT_FALSE(game::chat::ChatService::is_valid_text(blank.data(), blank.size(), 500));
}

TEST(ChatService, IsValidText_RejectsOverLimit) {
    const std::string big(501, 'x');
    EXPECT_FALSE(game::chat::ChatService::is_valid_text(big.data(), big.size(), 500));
}

TEST(ChatService, IsValidText_AcceptsExactlyAtLimit) {
    const std::string exact(500, 'x');
    EXPECT_TRUE(game::chat::ChatService::is_valid_text(exact.data(), exact.size(), 500));
}

// ── dispatch path: authenticated vs not ───────────────────────────────────

TEST(ChatService, UnauthenticatedSessionIsSilentlyDropped) {
    Fixture f;
    // conn 1 added, but NOT authenticated
    f.sessions.add(1, "ip");
    auto raw = build_chat_say(::chat::ChatChannel_Local, "hello");
    f.deliver(1, raw);

    EXPECT_TRUE(f.dispatch_log.out.empty());
    EXPECT_TRUE(f.private_log.out.empty());
}

TEST(ChatService, UnknownConnIsDropped) {
    Fixture f;
    auto raw = build_chat_say(::chat::ChatChannel_Local, "hello");
    f.deliver(/*conn=*/999, raw);

    EXPECT_TRUE(f.dispatch_log.out.empty());
    EXPECT_TRUE(f.private_log.out.empty());
}

// ── dispatch path: successful fanout ──────────────────────────────────────

TEST(ChatService, ValidLocalMessageReachesAllAuthenticatedViaFlush) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    f.add_authed(2, 200, "bob");
    // conn 3 never authed — should be excluded.
    f.sessions.add(3, "ip");

    auto raw = build_chat_say(::chat::ChatChannel_Local, "hello");
    f.deliver(1, raw);

    // Chat emits onto the event dispatcher; routing happens on flush().
    f.dispatcher.flush();

    ASSERT_EQ(f.dispatch_log.out.size(), 2u);
    EXPECT_TRUE(f.private_log.out.empty());

    // Parse one of the emitted frames and verify ChatMessage shape.
    const auto& [conn, bytes] = f.dispatch_log.out.front();
    (void)conn;
    auto frame = game::net::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(frame);
    ASSERT_EQ(frame->packet_id, game::net::packet_id("chat.ChatMessage"));
    const auto* msg = flatbuffers::GetRoot<::chat::ChatMessage>(frame->payload);
    EXPECT_EQ(msg->speaker_account_id(), 100u);
    EXPECT_EQ(msg->channel(), ::chat::ChatChannel_Local);
    EXPECT_EQ(std::string(msg->text()->c_str(), msg->text()->size()), "hello");
    EXPECT_EQ(std::string(msg->speaker_name()->c_str(), msg->speaker_name()->size()), "alice");
}

// ── drop-silent paths ──────────────────────────────────────────────────────

TEST(ChatService, OversizedTextIsDroppedSilently) {
    Fixture f;
    f.add_authed(1, 100, "alice");

    const std::string too_big(501, 'x');
    auto raw = build_chat_say(::chat::ChatChannel_Local, too_big);
    f.deliver(1, raw);
    f.dispatcher.flush();

    EXPECT_TRUE(f.dispatch_log.out.empty());
    EXPECT_TRUE(f.private_log.out.empty());  // no ChatThrottled either
}

TEST(ChatService, EmptyTextIsDroppedSilently) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    auto raw = build_chat_say(::chat::ChatChannel_Local, "");
    f.deliver(1, raw);
    f.dispatcher.flush();
    EXPECT_TRUE(f.dispatch_log.out.empty());
    EXPECT_TRUE(f.private_log.out.empty());
}

TEST(ChatService, WhitespaceOnlyIsDroppedSilently) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    auto raw = build_chat_say(::chat::ChatChannel_Local, "   \t\n  ");
    f.deliver(1, raw);
    f.dispatcher.flush();
    EXPECT_TRUE(f.dispatch_log.out.empty());
}

// ── rate-limit / throttled path ──────────────────────────────────────────

TEST(ChatService, ExceedingBucketSendsThrottledToSenderOnly) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    f.add_authed(2, 200, "bob");

    auto raw = build_chat_say(::chat::ChatChannel_Local, "hi");
    // First 5 succeed (cap = 5)
    for (int i = 0; i < 5; ++i) f.deliver(1, raw);
    // 6th and 7th are throttled
    f.deliver(1, raw);
    f.deliver(1, raw);
    f.dispatcher.flush();

    // 5 ChatMessage fanouts × 2 recipients = 10 dispatch-log entries
    EXPECT_EQ(f.dispatch_log.out.size(), 10u);
    // Two ChatThrottled → only conn 1 (sender), not conn 2.
    ASSERT_EQ(f.private_log.out.size(), 2u);
    for (const auto& [conn, bytes] : f.private_log.out) {
        EXPECT_EQ(conn, 1u);
        auto frame = game::net::decode(bytes.data(), bytes.size());
        ASSERT_TRUE(frame);
        EXPECT_EQ(frame->packet_id, game::net::packet_id("chat.ChatThrottled"));
        const auto* msg = flatbuffers::GetRoot<::chat::ChatThrottled>(frame->payload);
        EXPECT_EQ(msg->channel(), ::chat::ChatChannel_Local);
        EXPECT_GT(msg->retry_after_ms(), 0u);
    }
}

TEST(ChatService, RateLimitIsolatedPerAccount) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    f.add_authed(2, 200, "bob");

    auto raw = build_chat_say(::chat::ChatChannel_Local, "hi");
    // Alice exhausts her Local bucket.
    for (int i = 0; i < 5; ++i) f.deliver(1, raw);
    f.deliver(1, raw);  // → throttled (alice)

    // Bob still has a full bucket and is unaffected.
    f.deliver(2, raw);
    f.dispatcher.flush();

    // 1 throttle sent to alice
    ASSERT_EQ(f.private_log.out.size(), 1u);
    EXPECT_EQ(f.private_log.out.front().first, 1u);
}

// ── server_tick stamping ──────────────────────────────────────────────────

TEST(ChatService, ChatMessageCarriesServerTick) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    f.add_authed(2, 200, "bob");
    // Advance the sim loop's tick by driving one synchronous tick.
    f.sim_loop.run_one_tick_for_test();
    f.sim_loop.run_one_tick_for_test();
    f.sim_loop.run_one_tick_for_test();

    auto raw = build_chat_say(::chat::ChatChannel_Local, "hello");
    f.deliver(1, raw);
    f.dispatcher.flush();

    ASSERT_GE(f.dispatch_log.out.size(), 1u);
    const auto& bytes = f.dispatch_log.out.front().second;
    auto frame = game::net::decode(bytes.data(), bytes.size());
    ASSERT_TRUE(frame);
    const auto* msg = flatbuffers::GetRoot<::chat::ChatMessage>(frame->payload);
    EXPECT_EQ(msg->server_tick(), 3u);
}

// ── channel handling ──────────────────────────────────────────────────────

TEST(ChatService, GlobalChannelUsesGlobalScope) {
    Fixture f;
    f.add_authed(1, 100, "alice");
    f.add_authed(2, 200, "bob");

    auto raw = build_chat_say(::chat::ChatChannel_Global, "hello everyone");
    f.deliver(1, raw);
    f.dispatcher.flush();

    // Global currently fans out to every authenticated session (same as Local
    // in step-006) — 2 sends.
    EXPECT_EQ(f.dispatch_log.out.size(), 2u);
    const auto& bytes = f.dispatch_log.out.front().second;
    auto frame = game::net::decode(bytes.data(), bytes.size());
    const auto* msg = flatbuffers::GetRoot<::chat::ChatMessage>(frame->payload);
    EXPECT_EQ(msg->channel(), ::chat::ChatChannel_Global);
}
