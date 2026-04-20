#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace game::net {

// Wire frame layout: [uint32 packet_id little-endian][payload bytes].
// The payload length is implicit — one WebSocket binary frame = one packet.

using PacketId = uint32_t;

// FNV-1a 32-bit, `constexpr` so packet ids can be baked into switch/case
// tables at compile time. Same algorithm is used on the client so both sides
// independently arrive at the same id for e.g. "auth.Login".
constexpr PacketId packet_id(std::string_view name) noexcept {
    PacketId hash = 0x811C9DC5u;
    for (char c : name) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x01000193u;
    }
    return hash;
}

// Returned by decode() — points into the caller-owned buffer, so callers
// must not use a Frame after the backing bytes go out of scope.
struct Frame {
    PacketId packet_id = 0;
    const uint8_t* payload = nullptr;
    size_t payload_len = 0;
};

// Encode a framed packet. `payload` may be null if `payload_len == 0`.
std::vector<uint8_t> encode(PacketId id, const uint8_t* payload, size_t payload_len);

// Decode a framed packet. Returns std::nullopt if the buffer is too short
// to contain a packet id header.
std::optional<Frame> decode(const uint8_t* data, size_t len) noexcept;

} // namespace game::net
