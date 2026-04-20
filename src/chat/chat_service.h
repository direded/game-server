#pragma once

#include "chat/rate_limiter.h"
#include "net/transport.h"
#include "protocol/generated/chat_generated.h"
#include "session/session_manager.h"

#include <chrono>
#include <cstdint>
#include <functional>

namespace game::event { class EventDispatcher; }
namespace game::sim   { class SimLoop; }

namespace game::chat {

// Handler for `chat.ChatSay`. Runs on IO threads — the chat path deliberately
// bypasses the sim command queue so a chatty room can't starve gameplay
// commands. Mutates no world state; hands a pre-framed ChatMessage to the
// event dispatcher for next-tick fanout.
class ChatService {
public:
    using Sender = std::function<void(net::ConnId, const uint8_t*, size_t)>;

    struct Config {
        size_t max_text_bytes = 500;  // UTF-8 byte length cap
    };

    ChatService(session::SessionManager& sessions,
                RateLimiter& rate_limiter,
                game::event::EventDispatcher& events,
                game::sim::SimLoop& sim_loop,
                Sender private_sender,
                Config cfg = {});

    // Called by the network dispatcher on the IO thread.
    void handle_chat_say(net::ConnId conn, const ::chat::ChatSay& pkt);

    // Exposed for tests — trim + UTF-8-byte-length validation only, no
    // session/rate-limit checks. Returns true if the text is acceptable.
    static bool is_valid_text(const char* data, size_t len, size_t max_bytes);

private:
    void send_throttled(net::ConnId conn, ::chat::ChatChannel channel,
                        std::chrono::milliseconds retry_after);

    session::SessionManager& sessions_;
    RateLimiter& rate_limiter_;
    game::event::EventDispatcher& events_;
    game::sim::SimLoop& sim_loop_;
    Sender private_sender_;
    Config cfg_;
};

} // namespace game::chat
