#include "sim/action_resolver.h"

#include "event/dispatcher.h"
#include "log/logger.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "protocol/generated/world_generated.h"
#include "session/session_manager.h"
#include "world/world.h"

#include <flatbuffers/flatbuffers.h>

#include <vector>

namespace game::sim {

namespace {

std::vector<uint8_t> build_character_left(world::CharacterId id,
                                          const std::string& name,
                                          world::LocationId to_loc) {
    flatbuffers::FlatBufferBuilder fbb;
    auto name_off = fbb.CreateString(name);
    auto root = ::world::CreateCharacterLeft(fbb, id, name_off, to_loc);
    fbb.Finish(root);
    return net::encode(net::packet_id("world.CharacterLeft"),
                       fbb.GetBufferPointer(), fbb.GetSize());
}

std::vector<uint8_t> build_character_entered(world::CharacterId id,
                                             const std::string& name,
                                             world::LocationId from_loc) {
    flatbuffers::FlatBufferBuilder fbb;
    auto name_off = fbb.CreateString(name);
    auto root = ::world::CreateCharacterEntered(fbb, id, name_off, from_loc);
    fbb.Finish(root);
    return net::encode(net::packet_id("world.CharacterEntered"),
                       fbb.GetBufferPointer(), fbb.GetSize());
}

std::vector<uint8_t> build_action_completed(world::CharacterId id,
                                            world::ActionKind kind) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::action::CreateActionCompleted(
        fbb, id, static_cast<::action::ActionKind>(kind));
    fbb.Finish(root);
    return net::encode(net::packet_id("action.ActionCompleted"),
                       fbb.GetBufferPointer(), fbb.GetSize());
}

} // namespace

void ActionResolver::tick(world::World& world, event::EventDispatcher& events) {
    // Two-phase to avoid mutating the characters map while iterating it: we
    // never add/remove characters here, but we do mutate per-character state,
    // and a Travel completion mutates location.characters sets. Snapshot the
    // ids first.
    std::vector<world::CharacterId> active;
    active.reserve(world.characters().size());
    for (const auto& [id, ch] : world.characters()) {
        if (ch.current_action.has_value()) active.push_back(id);
    }

    for (auto id : active) {
        auto* ch = world.character(id);
        if (!ch || !ch->current_action.has_value()) continue;
        auto& action = *ch->current_action;
        ++action.elapsed_ticks;
        if (action.elapsed_ticks < action.duration_ticks) continue;

        // ── Action complete — resolve by kind ─────────────────────────────
        switch (action.kind) {
            case world::ActionKind::Travel: {
                const auto* params = std::get_if<world::TravelParams>(&action.params);
                if (!params) {
                    LOG_ERR("action_resolver: Travel without TravelParams (char {})", id);
                    ch->current_action.reset();
                    break;
                }
                const auto from_loc = ch->location_id;
                const auto to_loc = params->to;
                const auto kind = action.kind;
                const std::string name = ch->name;  // copy before move

                // Clear action *before* moving so the character is settled
                // when the broadcasts go out.
                ch->current_action.reset();
                if (!world.move_character(id, to_loc)) {
                    LOG_ERR("action_resolver: move_character {} -> {} failed", id, to_loc);
                    break;
                }

                // Keep the bound session's cached location_id current so
                // chat-Local routing tracks the move on the next packet.
                if (auto conn = sessions_.session_for_character(id)) {
                    sessions_.update_character_location(*conn, to_loc);
                }

                // Broadcast: leave-old, enter-new.
                {
                    event::Event ev;
                    ev.scope = event::EventScope::Local;
                    ev.local_location = from_loc;
                    ev.packet_id = net::packet_id("world.CharacterLeft");
                    ev.payload = build_character_left(id, name, to_loc);
                    events.emit(std::move(ev));
                }
                {
                    event::Event ev;
                    ev.scope = event::EventScope::Local;
                    ev.local_location = to_loc;
                    ev.packet_id = net::packet_id("world.CharacterEntered");
                    ev.payload = build_character_entered(id, name, from_loc);
                    events.emit(std::move(ev));
                }

                // Private ActionCompleted to the actor's session, if any.
                if (auto conn = sessions_.session_for_character(id)) {
                    event::Event ev;
                    ev.scope = event::EventScope::Private;
                    ev.private_target = *conn;
                    ev.packet_id = net::packet_id("action.ActionCompleted");
                    ev.payload = build_action_completed(id, kind);
                    events.emit(std::move(ev));
                }
                break;
            }
        }
    }
}

} // namespace game::sim
