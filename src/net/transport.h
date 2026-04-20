#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace game::net {

using ConnId = uint64_t;

// Abstract WebSocket transport. The IX-based implementation lives in
// ix_transport.{h,cpp}; nothing above this header should #include anything
// from ixwebsocket directly so we can swap the backend later (TLS, uWS, etc.)
// without touching the dispatch/session/auth layers.
//
// Callbacks are invoked on IO thread(s) owned by the implementation. The
// implementation must serialize callbacks for the same ConnId — handlers can
// assume a connection's on_connect / on_message / on_disconnect do not race
// with each other, though callbacks for different ConnIds may run concurrently.
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void start(uint16_t port) = 0;
    virtual void stop() = 0;

    virtual void send(ConnId conn_id, const uint8_t* data, size_t len) = 0;
    virtual void disconnect(ConnId conn_id) = 0;

    std::function<void(ConnId, std::string_view remote_addr)> on_connect;
    std::function<void(ConnId)> on_disconnect;
    std::function<void(ConnId, const uint8_t*, size_t)> on_message;
};

} // namespace game::net
