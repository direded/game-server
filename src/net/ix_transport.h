#pragma once

#include "net/transport.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// Forward-declare ixwebsocket types to keep the header clean — callers never
// need to include ixwebsocket headers. The impl file does.
namespace ix {
class WebSocket;
class WebSocketServer;
} // namespace ix

namespace game::net {

// ixwebsocket-backed transport. Implementation detail: connection ids are
// assigned monotonically by a local atomic counter; the id ↔ WebSocket map
// is guarded by a mutex. One connection per thread (ixwebsocket default).
class IxTransport : public ITransport {
public:
    IxTransport();
    ~IxTransport() override;

    // Soft cap on concurrent connections. Connections beyond this are
    // accepted and immediately closed with a "server full" reason.
    void set_max_connections(size_t max);

    void start(uint16_t port) override;
    void stop() override;
    void send(ConnId conn_id, const uint8_t* data, size_t len) override;
    void disconnect(ConnId conn_id) override;

private:
    std::unique_ptr<ix::WebSocketServer> server_;
    std::atomic<ConnId> next_conn_id_{1};
    size_t max_connections_ = 10000;

    mutable std::mutex mu_;
    // Use weak_ptr so a closed connection's WebSocket is cleaned up by
    // ixwebsocket on schedule; send()/disconnect() lock() to get a temporary
    // strong reference for the actual call.
    std::unordered_map<ConnId, std::weak_ptr<ix::WebSocket>> connections_;
};

} // namespace game::net
