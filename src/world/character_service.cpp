#include "world/character_service.h"

#include "log/logger.h"
#include "net/framing.h"
#include "protocol/generated/action_generated.h"
#include "protocol/generated/world_generated.h"
#include "sim/commands/add_character.h"
#include "sim/commands/cancel_action.h"
#include "sim/commands/character_disconnect.h"
#include "sim/commands/deselect_character.h"
#include "sim/commands/select_character.h"
#include "sim/commands/start_action.h"
#include "sim/sim_loop.h"

#include <flatbuffers/flatbuffers.h>

#include <exception>
#include <memory>
#include <vector>

namespace game::world {

namespace {

bool is_unique_violation(const std::exception& e) {
    std::string_view msg = e.what();
    return msg.find("23505") != std::string_view::npos
        || msg.find("duplicate key") != std::string_view::npos
        || msg.find("unique constraint") != std::string_view::npos;
}

} // namespace

CharacterService::CharacterService(session::SessionManager& sessions,
                                   CharacterStore& store,
                                   sim::SimLoop& sim_loop,
                                   net::ITransport& transport,
                                   LocationId spawn_location_id,
                                   Sender private_sender)
    : sessions_(sessions), store_(store), sim_loop_(sim_loop),
      transport_(transport), spawn_location_id_(spawn_location_id),
      private_sender_(std::move(private_sender)) {}

void CharacterService::send_character_rejected(net::ConnId conn, uint8_t reason) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::world::CreateCharacterRejected(
        fbb, static_cast<::world::CharacterRejectReason>(reason));
    fbb.Finish(root);
    auto framed = net::encode(net::packet_id("world.CharacterRejected"),
                              fbb.GetBufferPointer(), fbb.GetSize());
    private_sender_(conn, framed.data(), framed.size());
}

void CharacterService::send_action_rejected(net::ConnId conn, uint8_t reason) {
    flatbuffers::FlatBufferBuilder fbb;
    auto root = ::action::CreateActionRejected(
        fbb, static_cast<::action::ActionRejectReason>(reason));
    fbb.Finish(root);
    auto framed = net::encode(net::packet_id("action.ActionRejected"),
                              fbb.GetBufferPointer(), fbb.GetSize());
    private_sender_(conn, framed.data(), framed.size());
}

void CharacterService::handle_create_character(net::ConnId conn,
                                               const ::world::CreateCharacter& pkt) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated || !sess->account_id) {
        LOG_DBG("world: drop CreateCharacter — unauthenticated conn {}", conn);
        return;
    }

    const auto* name_fb = pkt.name();
    const std::string name = name_fb ? name_fb->str() : std::string{};
    if (!is_valid_character_name(name)) {
        send_character_rejected(conn, static_cast<uint8_t>(
            ::world::CharacterRejectReason_NameInvalid));
        return;
    }

    Character created;
    try {
        created = store_.create(*sess->account_id, name, spawn_location_id_);
    } catch (const std::exception& e) {
        if (is_unique_violation(e)) {
            send_character_rejected(conn, static_cast<uint8_t>(
                ::world::CharacterRejectReason_NameTaken));
            return;
        }
        LOG_ERR("world: CreateCharacter store failed: {}", e.what());
        send_character_rejected(conn, static_cast<uint8_t>(
            ::world::CharacterRejectReason_ServerError));
        return;
    }
    LOG_INF("world: character created id={} account={} name={}",
            created.id, *sess->account_id, name);

    // Hand off to the sim thread to insert into World and reply with the
    // CharacterSummary. Doing the reply from inside the sim command keeps the
    // ordering invariant that subsequent SelectCharacter packets always find
    // the new character in World.
    sim_loop_.push_command(std::make_unique<sim::AddCharacterCommand>(conn, std::move(created)));
}

void CharacterService::handle_list_characters(net::ConnId conn,
                                              const ::world::ListCharacters& /*pkt*/) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated || !sess->account_id) {
        LOG_DBG("world: drop ListCharacters — unauthenticated conn {}", conn);
        return;
    }

    std::vector<Character> chars;
    try {
        chars = store_.list_by_account(*sess->account_id);
    } catch (const std::exception& e) {
        LOG_ERR("world: ListCharacters store failed: {}", e.what());
        send_character_rejected(conn, static_cast<uint8_t>(
            ::world::CharacterRejectReason_ServerError));
        return;
    }

    flatbuffers::FlatBufferBuilder fbb;
    std::vector<flatbuffers::Offset<::world::CharacterSummary>> rows;
    rows.reserve(chars.size());
    for (const auto& c : chars) {
        auto name_off = fbb.CreateString(c.name);
        rows.push_back(::world::CreateCharacterSummary(fbb, c.id, name_off, c.location_id));
    }
    auto vec = fbb.CreateVector(rows);
    auto root = ::world::CreateCharacterList(fbb, vec);
    fbb.Finish(root);

    auto framed = net::encode(net::packet_id("world.CharacterList"),
                              fbb.GetBufferPointer(), fbb.GetSize());
    private_sender_(conn, framed.data(), framed.size());
}

void CharacterService::handle_select_character(net::ConnId conn,
                                               const ::world::SelectCharacter& pkt) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated) {
        LOG_DBG("world: drop SelectCharacter — unauthenticated conn {}", conn);
        return;
    }

    const auto char_id = static_cast<CharacterId>(pkt.id());
    sim_loop_.push_command(std::make_unique<sim::SelectCharacterCommand>(
        conn, char_id, sessions_, transport_));
}

void CharacterService::handle_deselect_character(net::ConnId conn,
                                                 const ::world::DeselectCharacter& /*pkt*/) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated) {
        LOG_DBG("world: drop DeselectCharacter — unauthenticated conn {}", conn);
        return;
    }
    sim_loop_.push_command(std::make_unique<sim::DeselectCharacterCommand>(conn, sessions_));
}

void CharacterService::handle_start_action(net::ConnId conn,
                                           const ::action::StartAction& pkt) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated) {
        LOG_DBG("world: drop StartAction — unauthenticated conn {}", conn);
        return;
    }
    if (!sess->character_id) {
        send_action_rejected(conn, static_cast<uint8_t>(
            ::action::ActionRejectReason_NoCharacterSelected));
        return;
    }

    // Today the only kind is Travel. The union accessor returns nullptr if
    // the kind is missing/unknown.
    const auto kind_type = pkt.kind_type();
    if (kind_type == ::action::ActionStartKind_TravelActionStart) {
        const auto* travel = pkt.kind_as_TravelActionStart();
        if (!travel) {
            send_action_rejected(conn, static_cast<uint8_t>(
                ::action::ActionRejectReason_UnknownAction));
            return;
        }
        sim_loop_.push_command(std::make_unique<sim::StartActionCommand>(
            conn, *sess->character_id, ActionKind::Travel,
            sim::StartActionCommand::Params{TravelParams{travel->to_location_id()}}));
        return;
    }

    send_action_rejected(conn, static_cast<uint8_t>(
        ::action::ActionRejectReason_UnknownAction));
}

void CharacterService::handle_cancel_action(net::ConnId conn,
                                            const ::action::CancelAction& /*pkt*/) {
    auto sess = sessions_.get(conn);
    if (!sess || sess->state != session::AuthState::Authenticated) {
        LOG_DBG("world: drop CancelAction — unauthenticated conn {}", conn);
        return;
    }
    if (!sess->character_id) {
        send_action_rejected(conn, static_cast<uint8_t>(
            ::action::ActionRejectReason_NoCharacterSelected));
        return;
    }
    sim_loop_.push_command(std::make_unique<sim::CancelActionCommand>(
        conn, *sess->character_id));
}

void CharacterService::handle_disconnect(net::ConnId conn) {
    auto sess = sessions_.get(conn);
    if (!sess || !sess->character_id) return;
    sim_loop_.push_command(std::make_unique<sim::CharacterDisconnectCommand>(
        conn, *sess->character_id));
}

} // namespace game::world
