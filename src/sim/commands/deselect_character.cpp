#include "sim/commands/deselect_character.h"

#include "event/dispatcher.h"
#include "event/event.h"
#include "net/framing.h"
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

void DeselectCharacterCommand::execute(CommandContext& ctx) {
    auto sess = sessions_.get(conn_);
    if (!sess) return;
    if (!sess->character_id) {
        send_rejected(ctx, conn_, ::world::CharacterRejectReason_NoCharacter);
        return;
    }

    const auto char_id = *sess->character_id;
    auto* ch = ctx.world.character(char_id);
    if (!ch) {
        // Inconsistent state — session pointed at a character not in World.
        // Still unbind the session so it can pick another.
        sessions_.unbind_character(conn_);
        send_rejected(ctx, conn_, ::world::CharacterRejectReason_Unknown);
        return;
    }

    const auto location_id = ch->location_id;
    const std::string name = ch->name;
    ch->current_action.reset();
    ctx.world.remove_character_from_location(char_id, location_id);
    sessions_.unbind_character(conn_);

    // Broadcast CharacterLeft to the location with to_location_id = 0 (the
    // character isn't traveling; the field convention is "0 means the character
    // simply went offline / deselected here").
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto name_off = fbb.CreateString(name);
        auto root = ::world::CreateCharacterLeft(fbb, char_id, name_off, 0);
        fbb.Finish(root);
        auto pid = net::packet_id("world.CharacterLeft");
        auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

        event::Event ev;
        ev.scope = event::EventScope::Local;
        ev.local_location = location_id;
        ev.packet_id = pid;
        ev.payload = std::move(framed);
        ctx.events.emit(std::move(ev));
    }

    // Private CharacterDeselected reply.
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto root = ::world::CreateCharacterDeselected(fbb);
        fbb.Finish(root);
        auto pid = net::packet_id("world.CharacterDeselected");
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
