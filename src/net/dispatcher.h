#pragma once

#include "net/framing.h"
#include "net/transport.h"

#include <flatbuffers/flatbuffers.h>

#include <functional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace game::net {

// Routes inbound framed packets to per-type handlers. One instance lives in
// main(); handlers are registered at startup before the transport begins
// accepting connections.
//
// Thread-safety: register_handler() is not thread-safe and must only be called
// during single-threaded startup. dispatch() is safe to call concurrently
// (lookups are const on the map) — the transport's serialization guarantee
// means per-connection handlers still see in-order calls.
class Dispatcher {
public:
    template <typename PacketT>
    void register_handler(std::string_view name,
                          std::function<void(ConnId, const PacketT&)> handler) {
        const PacketId id = packet_id(name);
        handlers_[id] = [h = std::move(handler)](ConnId conn, const uint8_t* data, size_t len) {
            flatbuffers::Verifier verifier(data, len);
            if (!verifier.VerifyBuffer<PacketT>(nullptr)) {
                // Malformed payload — drop silently. A hostile peer can flood
                // these; logging every one invites log-volume attacks.
                return;
            }
            const PacketT* obj = flatbuffers::GetRoot<PacketT>(data);
            h(conn, *obj);
        };
    }

    // Decode + dispatch a single inbound message. Unknown packet ids are
    // logged at WARN and dropped; the connection is not closed so a client
    // running ahead of a server rollout can still function.
    void dispatch(ConnId conn, const uint8_t* data, size_t len) const;

private:
    using RawHandler = std::function<void(ConnId, const uint8_t*, size_t)>;
    std::unordered_map<PacketId, RawHandler> handlers_;
};

} // namespace game::net
