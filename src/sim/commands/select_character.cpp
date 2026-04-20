#include "sim/commands/select_character.h"

#include "event/dispatcher.h"
#include "event/event.h"
#include "log/logger.h"
#include "net/framing.h"
#include "net/transport.h"
#include "protocol/generated/world_generated.h"
#include "session/session_manager.h"
#include "world/world.h"

#include <flatbuffers/flatbuffers.h>

namespace game::sim {

namespace {

void send_rejected(CommandContext& ctx, net::ConnId conn,
                   ::world::CharacterRejectReason reason) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::world::CreateCharacterRejected(fbb, reason);
    fbb.Finish(root);
    auto pid = net::packet_id("world.CharacterRejected");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    event::Event ev;
    ev.scope = event::EventScope::Private;
    ev.private_target = conn;
    ev.packet_id = pid;
    ev.payload = std::move(framed);
    ctx.events.emit(std::move(ev));
}

} // namespace

void SelectCharacterCommand::execute(CommandContext& ctx) {
    auto sess = sessions_.get(conn_);
    if (!sess || sess->state != session::AuthState::Authenticated || !sess->account_id) {
        // The session disconnected or de-authed between IO enqueue and now.
        // Nothing to reply to in any meaningful way.
        return;
    }
    if (sess->character_id) {
        send_rejected(ctx, conn_, ::world::CharacterRejectReason_AlreadySelected);
        return;
    }

    auto* ch = ctx.world.character(char_id_);
    if (!ch) {
        send_rejected(ctx, conn_, ::world::CharacterRejectReason_Unknown);
        return;
    }
    if (ch->account_id != *sess->account_id) {
        send_rejected(ctx, conn_, ::world::CharacterRejectReason_NotOwned);
        return;
    }

    const auto location_id = ch->location_id;
    const std::string name = ch->name;  // copy for broadcast

    // Bind on the session manager. If the same character is already bound to
    // a different connection, kick that connection.
    auto kicked = sessions_.bind_character(conn_, char_id_, location_id);
    if (kicked) {
        LOG_INF("world: kicking conn={} (replaced by conn={} for char={})",
                *kicked, conn_, char_id_);
        transport_.disconnect(*kicked);
    }

    ctx.world.place_character_in_location(char_id_, location_id);

    // Broadcast CharacterEntered to the destination location. from_location_id
    // is 0 here — Select is not a Travel completion; the client distinguishes
    // by checking from_location_id != 0.
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto name_off = fbb.CreateString(name);
        auto root = ::world::CreateCharacterEntered(fbb, char_id_, name_off, 0);
        fbb.Finish(root);
        auto pid = net::packet_id("world.CharacterEntered");
        auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

        event::Event ev;
        ev.scope = event::EventScope::Local;
        ev.local_location = location_id;
        ev.packet_id = pid;
        ev.payload = std::move(framed);
        ctx.events.emit(std::move(ev));
    }

    // Private CharacterSelected to the requesting session.
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto name_off = fbb.CreateString(name);
        auto root = ::world::CreateCharacterSelected(fbb, char_id_, name_off, location_id);
        fbb.Finish(root);
        auto pid = net::packet_id("world.CharacterSelected");
        auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

        event::Event ev;
        ev.scope = event::EventScope::Private;
        ev.private_target = conn_;
        ev.packet_id = pid;
        ev.payload = std::move(framed);
        ctx.events.emit(std::move(ev));
    }
}

} // namespace game::sim
