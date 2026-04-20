#include "sim/commands/add_character.h"

#include "event/dispatcher.h"
#include "event/event.h"
#include "net/framing.h"
#include "protocol/generated/world_generated.h"
#include "world/world.h"

#include <flatbuffers/flatbuffers.h>

namespace game::sim {

void AddCharacterCommand::execute(CommandContext& ctx) {
    const auto id = character_.id;
    const auto location_id = character_.location_id;
    const std::string name = character_.name;  // copy before move
    ctx.world.add_character(std::move(character_));

    flatbuffers::FlatBufferBuilder fbb;
    auto name_off = fbb.CreateString(name);
    auto root = ::world::CreateCharacterSummary(fbb, id, name_off, location_id);
    fbb.Finish(root);
    auto pid = net::packet_id("world.CharacterSummary");
    auto framed = net::encode(pid, fbb.GetBufferPointer(), fbb.GetSize());

    event::Event ev;
    ev.scope = event::EventScope::Private;
    ev.private_target = conn_;
    ev.packet_id = pid;
    ev.payload = std::move(framed);
    ctx.events.emit(std::move(ev));
}

} // namespace game::sim
