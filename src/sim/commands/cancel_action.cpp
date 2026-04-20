#include "sim/commands/cancel_action.h"

#include "event/dispatcher.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "world/world.h"

#include <flatbuffers/flatbuffers.h>

namespace game::sim {

void CancelActionCommand::execute(CommandContext& ctx) {
    auto* ch = ctx.world.character(char_id_);
    if (!ch || !ch->current_action.has_value()) {
        // No-op — there is no action to cancel. Spec says nothing about
        // surfacing this; cancel is idempotent from the client's POV.
        return;
    }
    const auto kind = ch->current_action->kind;
    ch->current_action.reset();

    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::action::CreateActionCancelled(
        fbb, char_id_, static_cast<::action::ActionKind>(kind));
    fbb.Finish(root);
    const auto pid = net::packet_id("action.ActionCancelled");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    // Local broadcast to current location.
    event::Event broadcast;
    broadcast.scope = event::EventScope::Local;
    broadcast.local_location = ch->location_id;
    broadcast.packet_id = pid;
    broadcast.payload = framed;  // copy — also sent privately below
    ctx.events.emit(std::move(broadcast));

    // Private copy back to the requesting connection. Local fan-out already
    // includes the actor's own session if they are bound (and at the same
    // location), so this is technically redundant in the common case — but
    // the spec calls for an explicit private copy and it covers the rare
    // case where the actor's session got rebound mid-tick.
    event::Event private_ev;
    private_ev.scope = event::EventScope::Private;
    private_ev.private_target = conn_;
    private_ev.packet_id = pid;
    private_ev.payload = std::move(framed);
    ctx.events.emit(std::move(private_ev));
}

} // namespace game::sim
