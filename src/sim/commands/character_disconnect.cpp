#include "sim/commands/character_disconnect.h"

#include "event/dispatcher.h"
#include "event/event.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "protocol/generated/world_generated.h"
#include "world/world.h"

#include <flatbuffers/flatbuffers.h>

namespace game::sim {

void CharacterDisconnectCommand::execute(CommandContext& ctx) {
    auto* ch = ctx.world.character(char_id_);
    if (!ch) return;  // race: character somehow already gone

    const auto location_id = ch->location_id;
    const std::string name = ch->name;

    // Cancel any active action and broadcast ActionCancelled to the location
    // so co-located observers see it stop. No private copy — there is no
    // session left to receive one.
    if (ch->current_action.has_value()) {
        const auto kind = ch->current_action->kind;
        ch->current_action.reset();

        flatbuffers::FlatBufferBuilder fbb;
        auto root = ::action::CreateActionCancelled(
            fbb, char_id_, static_cast<::action::ActionKind>(kind));
        fbb.Finish(root);
        auto pid = net::packet_id("action.ActionCancelled");
        auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

        event::Event ev;
        ev.scope = event::EventScope::Local;
        ev.local_location = location_id;
        ev.packet_id = pid;
        ev.payload = std::move(framed);
        ctx.events.emit(std::move(ev));
    }

    // Remove from location set so no further Local broadcasts include this id.
    ctx.world.remove_character_from_location(char_id_, location_id);

    // Broadcast CharacterLeft so co-located clients can drop the avatar.
    flatbuffers::FlatBufferBuilder fbb;
    auto name_off = fbb.CreateString(name);
    auto root = ::world::CreateCharacterLeft(fbb, char_id_, name_off, 0);
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

} // namespace game::sim
