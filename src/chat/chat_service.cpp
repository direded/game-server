#include "chat/chat_service.h"

#include "event/dispatcher.h"
#include "event/event.h"
#include "log/logger.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "protocol/generated/chat_generated.h"
#include "sim/sim_loop.h"

#include <flatbuffers/flatbuffers.h>

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace game::chat {

namespace {

// Map wire enum → rate-limiter enum. Kept as a free function so rate_limiter.h
// doesn't need to include chat_generated.h.
Channel to_rl_channel(::chat::ChatChannel c) {
    return c == ::chat::ChatChannel_Global ? Channel::Global : Channel::Local;
}

bool is_known_channel(::chat::ChatChannel c) {
    return c == ::chat::ChatChannel_Local || c == ::chat::ChatChannel_Global;
}

} // namespace

ChatService::ChatService(session::SessionManager& sessions,
                         RateLimiter& rate_limiter,
                         game::event::EventDispatcher& events,
                         game::sim::SimLoop& sim_loop,
                         Sender private_sender,
                         Config cfg)
    : sessions_(sessions), rate_limiter_(rate_limiter), events_(events),
      sim_loop_(sim_loop), private_sender_(std::move(private_sender)), cfg_(cfg) {}

bool ChatService::is_valid_text(const char* data, size_t len, size_t max_bytes) {
    if (data == nullptr || len == 0) return false;
    if (len > max_bytes) return false;
    // Require at least one non-whitespace byte. Using ASCII whitespace only
    // is fine here — we reject empty + all-space strings, not pathological
    // unicode. Zero-width / control-char flooding is a separate problem.
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = static_cast<unsigned char>(data[i]);
        // Treat any byte > 0x20 as "content". Non-ASCII UTF-8 bytes are all
        // > 0x7F, so they all qualify.
        if (c > 0x20) return true;
    }
    return false;
}

void ChatService::send_throttled(net::ConnId conn, ::chat::ChatChannel channel,
                                 std::chrono::milliseconds retry_after) {
    flatbuffers::FlatBufferBuilder fbb;
    const uint32_t ms = static_cast<uint32_t>(
        std::clamp<int64_t>(retry_after.count(), 0, std::numeric_limits<uint32_t>::max()));
    auto root = ::chat::CreateChatThrottled(fbb, channel, ms);
    fbb.Finish(root);

    auto framed = net::encode(net::packet_id("chat.ChatThrottled"),
                              fbb.GetBufferPointer(), fbb.GetSize());
    private_sender_(conn, framed.data(), framed.size());
}

void ChatService::handle_chat_say(net::ConnId conn, const ::chat::ChatSay& pkt) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated) {
        LOG_DBG("chat: drop ChatSay — unauthenticated conn {}", conn);
        return;
    }

    const auto channel = pkt.channel();
    if (!is_known_channel(channel)) {
        LOG_DBG("chat: drop ChatSay — unknown channel {} conn {}",
                static_cast<int>(channel), conn);
        return;
    }

    const auto* text_fb = pkt.text();
    const char* text_data = text_fb ? text_fb->c_str() : nullptr;
    const size_t text_len = text_fb ? text_fb->size() : 0;
    if (!is_valid_text(text_data, text_len, cfg_.max_text_bytes)) {
        LOG_DBG("chat: drop ChatSay — invalid text len={} conn={}", text_len, conn);
        return;
    }

    // Local chat needs a selected character (it routes by location). Per the
    // step-007 spec, missing-character on a gameplay packet replies with
    // action.ActionRejected{NoCharacterSelected}. Global chat is account-level
    // and works without a character. Packet-shape validation runs first so
    // a malformed packet doesn't masquerade as a "no character selected" error.
    if (channel == ::chat::ChatChannel_Local && !sess->character_id) {
        flatbuffers::FlatBufferBuilder fbb;
        auto root = ::action::CreateActionRejected(
            fbb, ::action::ActionRejectReason_NoCharacterSelected);
        fbb.Finish(root);
        auto framed = net::encode(net::packet_id("action.ActionRejected"),
                                  fbb.GetBufferPointer(), fbb.GetSize());
        private_sender_(conn, framed.data(), framed.size());
        return;
    }

    const auto account_id = sess->account_id.value();
    const auto now = std::chrono::steady_clock::now();
    const auto rl_channel = to_rl_channel(channel);
    if (!rate_limiter_.try_consume(account_id, rl_channel, now)) {
        const auto retry = rate_limiter_.retry_after(account_id, rl_channel, now);
        LOG_DBG("chat: throttled account={} channel={} retry_after_ms={}",
                account_id, static_cast<int>(channel), retry.count());
        send_throttled(conn, channel, retry);
        return;
    }

    // Build the ChatMessage for fanout. server_tick is stamped here, at
    // build time; all recipients see the same tick value regardless of
    // which tick the dispatcher actually flushes on.
    flatbuffers::FlatBufferBuilder fbb;
    auto speaker_name = fbb.CreateString(sess->username);
    auto text_off = fbb.CreateString(text_data, text_len);
    const uint64_t server_tick = sim_loop_.tick_count();
    auto root = ::chat::CreateChatMessage(fbb, account_id, speaker_name,
                                          channel, text_off, server_tick);
    fbb.Finish(root);

    const auto pid = net::packet_id("chat.ChatMessage");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    game::event::Event ev;
    if (channel == ::chat::ChatChannel_Global) {
        ev.scope = game::event::EventScope::Global;
    } else {
        ev.scope = game::event::EventScope::Local;
        // Route by the speaker's cached session.location_id. The sim thread
        // updates this via SessionManager::update_character_location whenever
        // an action moves the character.
        ev.local_location = sess->location_id;
    }
    ev.packet_id = pid;
    ev.payload = std::move(framed);
    events_.emit(std::move(ev));
}

} // namespace game::chat
