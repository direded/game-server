#include "net/ix_transport.h"

#include "log/logger.h"

#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketMessageType.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <memory>
#include <string>

namespace game::net {

IxTransport::IxTransport() = default;

IxTransport::~IxTransport() {
    stop();
}

void IxTransport::set_max_connections(size_t max) {
    max_connections_ = max;
}

void IxTransport::start(uint16_t port) {
    // On Windows, socket() requires WSAStartup to have run. libpq does this
    // implicitly in the Postgres backend path, but the memory backend never
    // loads libpq, so we must initialize Winsock explicitly here. Idempotent.
    ix::initNetSystem();

    // backlog kept at ixwebsocket default; max-conn is enforced by us below
    // so we can send a polite close reason instead of dropping the TCP accept.
    server_ = std::make_unique<ix::WebSocketServer>(port);

    server_->setOnConnectionCallback(
        [this](std::weak_ptr<ix::WebSocket> ws_weak,
               std::shared_ptr<ix::ConnectionState> state) {
            auto ws = ws_weak.lock();
            if (!ws) return;

            const ConnId id = next_conn_id_.fetch_add(1, std::memory_order_relaxed);
            const std::string remote = state->getRemoteIp();

            size_t current_count = 0;
            {
                std::lock_guard<std::mutex> lg(mu_);
                current_count = connections_.size();
            }
            if (current_count >= max_connections_) {
                LOG_WRN("net: max connections ({}) reached; rejecting {}",
                        max_connections_, remote);
                ws->close(1013 /*Try Again Later*/, "server at capacity");
                return;
            }

            {
                std::lock_guard<std::mutex> lg(mu_);
                connections_.emplace(id, ws_weak);
            }

            ws->setOnMessageCallback(
                [this, id, remote](const ix::WebSocketMessagePtr& msg) {
                    switch (msg->type) {
                    case ix::WebSocketMessageType::Open:
                        LOG_DBG("net: conn {} opened from {}", id, remote);
                        if (on_connect) on_connect(id, remote);
                        break;
                    case ix::WebSocketMessageType::Message:
                        if (on_message) {
                            on_message(id,
                                       reinterpret_cast<const uint8_t*>(msg->str.data()),
                                       msg->str.size());
                        }
                        break;
                    case ix::WebSocketMessageType::Close:
                        LOG_DBG("net: conn {} closed", id);
                        {
                            std::lock_guard<std::mutex> lg(mu_);
                            connections_.erase(id);
                        }
                        if (on_disconnect) on_disconnect(id);
                        break;
                    case ix::WebSocketMessageType::Error:
                        LOG_WRN("net: conn {} error: {}", id, msg->errorInfo.reason);
                        break;
                    default:
                        break;
                    }
                });
        });

    auto res = server_->listen();
    if (!res.first) {
        const std::string err = res.second;
        server_.reset();
        throw std::runtime_error("WebSocket listen failed: " + err);
    }
    server_->start();
    LOG_INF("net: WebSocket server listening on ws://0.0.0.0:{}", port);
}

void IxTransport::stop() {
    if (!server_) return;
    LOG_INF("net: stopping WebSocket server");
    server_->stop();
    server_.reset();
    std::lock_guard<std::mutex> lg(mu_);
    connections_.clear();
}

void IxTransport::send(ConnId conn_id, const uint8_t* data, size_t len) {
    std::weak_ptr<ix::WebSocket> ws_weak;
    {
        std::lock_guard<std::mutex> lg(mu_);
        auto it = connections_.find(conn_id);
        if (it == connections_.end()) return;
        ws_weak = it->second;
    }
    if (auto ws = ws_weak.lock()) {
        // ixwebsocket's sendBinary takes a std::string; the string is a raw
        // byte container here — no text semantics.
        ws->sendBinary(std::string(reinterpret_cast<const char*>(data), len));
    }
}

void IxTransport::disconnect(ConnId conn_id) {
    std::weak_ptr<ix::WebSocket> ws_weak;
    {
        std::lock_guard<std::mutex> lg(mu_);
        auto it = connections_.find(conn_id);
        if (it == connections_.end()) return;
        ws_weak = it->second;
    }
    if (auto ws = ws_weak.lock()) {
        ws->close();
    }
}

} // namespace game::net
