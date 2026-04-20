#include "sim/sim_dispatcher.h"

#include "sim/commands/ping.h"
#include "sim/sim_loop.h"
#include "protocol/generated/sim_generated.h"

#include <memory>

namespace game::sim {

void SimDispatcher::handle_client_ping(net::ConnId conn, const ::sim::ClientPing& pkt) {
    sim_loop_.push_command(std::make_unique<PingCommand>(conn, pkt.nonce()));
}

} // namespace game::sim
