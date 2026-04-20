#include "sim/commands/start_action.h"

#include "event/dispatcher.h"
#include "log/logger.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "world/world.h"

#include <flatbuffers/flatbuffers.h>

namespace game::sim {

namespace {

void send_rejected(CommandContext& ctx, net::ConnId conn,
                   ::action::ActionRejectReason reason) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::action::CreateActionRejected(fbb, reason);
    fbb.Finish(root);
    auto pid = net::packet_id("action.ActionRejected");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    event::Event ev;
    ev.scope = event::EventScope::Private;
    ev.private_target = conn;
    ev.packet_id = pid;
    ev.payload = std::move(framed);
    ctx.events.emit(std::move(ev));
}

} // namespace

void StartActionCommand::execute(CommandContext& ctx) {
    auto* ch = ctx.world.character(char_id_);
    if (!ch) {
        // Character vanished between IO-thread enqueue and now. Treat as a
        // generic rejection — the session is almost certainly gone too.
        send_rejected(ctx, conn_, ::action::ActionRejectReason_NoCharacterSelected);
        return;
    }
    if (ch->current_action.has_value()) {
        send_rejected(ctx, conn_, ::action::ActionRejectReason_Busy);
        return;
    }

    if (kind_ != world::ActionKind::Travel) {
        send_rejected(ctx, conn_, ::action::ActionRejectReason_UnknownAction);
        return;
    }
    const auto* travel = std::get_if<world::TravelParams>(&params_);
    if (!travel) {
        send_rejected(ctx, conn_, ::action::ActionRejectReason_UnknownAction);
        return;
    }

    auto* from_loc = ctx.world.location(ch->location_id);
    if (!from_loc) {
        // Inconsistent state — character pointed at a missing location.
        LOG_ERR("sim: character {} has unknown location {}", char_id_, ch->location_id);
        send_rejected(ctx, conn_, ::action::ActionRejectReason_UnknownLocation);
        return;
    }
    const world::LocationLink* link = nullptr;
    for (const auto& l : from_loc->links) {
        if (l.to == travel->to) { link = &l; break; }
    }
    if (link == nullptr) {
        // Either the destination doesn't exist at all, or it exists but is
        // not directly linked from here. The wire reason distinguishes the
        // two so clients can render meaningful errors.
        const bool target_exists = ctx.world.location(travel->to) != nullptr;
        send_rejected(ctx, conn_,
                      target_exists ? ::action::ActionRejectReason_NotLinked
                                    : ::action::ActionRejectReason_UnknownLocation);
        return;
    }

    world::Action action;
    action.kind = world::ActionKind::Travel;
    action.started_tick = ctx.tick;
    action.duration_ticks = link->travel_ticks;
    action.elapsed_ticks = 0;
    action.params = world::TravelParams{travel->to};
    ch->current_action = action;

    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::action::CreateActionStarted(fbb, char_id_,
                                              ::action::ActionKind_Travel,
                                              link->travel_ticks,
                                              ctx.tick);
    fbb.Finish(root);
    const auto pid = net::packet_id("action.ActionStarted");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    event::Event ev;
    ev.scope = event::EventScope::Local;
    ev.local_location = ch->location_id;
    ev.packet_id = pid;
    ev.payload = std::move(framed);
    ctx.events.emit(std::move(ev));
}

} // namespace game::sim
