#include "sim/commands/ping.h"

#include "event/dispatcher.h"
#include "net/framing.h"
#include "protocol/generated/sim_generated.h"

#include <flatbuffers/flatbuffers.h>

namespace game::sim {

void PingCommand::execute(CommandContext& ctx) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::sim::CreateServerPong(fbb, nonce_, ctx.tick);
    fbb.Finish(root);

    const auto pid = net::packet_id("sim.ServerPong");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    game::event::Event ev;
    ev.scope = game::event::EventScope::Private;
    ev.private_target = conn_id_;
    ev.packet_id = pid;
    ev.payload = std::move(framed);
    ctx.events.emit(std::move(ev));
}

} // namespace game::sim
