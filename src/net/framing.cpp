#include "net/framing.h"

#include <cstring>

namespace game::net {

std::vector<uint8_t> encode(PacketId id, const uint8_t* payload, size_t payload_len) {
    std::vector<uint8_t> out(sizeof(PacketId) + payload_len);
    // Little-endian, matching protocol/README.md.
    out[0] = static_cast<uint8_t>(id & 0xFFu);
    out[1] = static_cast<uint8_t>((id >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((id >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((id >> 24) & 0xFFu);
    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(out.data() + sizeof(PacketId), payload, payload_len);
    }
    return out;
}

std::optional<Frame> decode(const uint8_t* data, size_t len) noexcept {
    if (data == nullptr || len < sizeof(PacketId)) {
        return std::nullopt;
    }
    Frame f;
    f.packet_id = static_cast<PacketId>(data[0])
                | (static_cast<PacketId>(data[1]) << 8)
                | (static_cast<PacketId>(data[2]) << 16)
                | (static_cast<PacketId>(data[3]) << 24);
    f.payload = data + sizeof(PacketId);
    f.payload_len = len - sizeof(PacketId);
    return f;
}

} // namespace game::net
