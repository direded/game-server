#include "net/dispatcher.h"

#include "log/logger.h"

namespace game::net {

void Dispatcher::dispatch(ConnId conn, const uint8_t* data, size_t len) const {
    auto frame_opt = decode(data, len);
    if (!frame_opt) {
        LOG_WRN("net: dropping short frame from conn {} ({} bytes)", conn, len);
        return;
    }
    const auto& frame = *frame_opt;

    auto it = handlers_.find(frame.packet_id);
    if (it == handlers_.end()) {
        LOG_WRN("net: unknown packet_id {} from conn {}", frame.packet_id, conn);
        return;
    }

    LOG_DBG("net: dispatch packet_id={} conn={} payload_len={}",
            frame.packet_id, conn, frame.payload_len);
    it->second(conn, frame.payload, frame.payload_len);
}

} // namespace game::net
