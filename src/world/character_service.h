#pragma once

#include "net/transport.h"
#include "session/session_manager.h"
#include "world/character_store.h"
#include "world/ids.h"

#include <cstddef>
#include <cstdint>
#include <functional>

// Forward-declare generated FlatBuffers tables so this header doesn't drag
// in protocol/generated/world_generated.h or action_generated.h.
namespace world {
struct CreateCharacter;
struct ListCharacters;
struct SelectCharacter;
struct DeselectCharacter;
}
namespace action {
struct StartAction;
struct CancelAction;
}

namespace game::sim { class SimLoop; }
namespace game::net { class ITransport; }

namespace game::world {

// IO-thread handler for the character lifecycle and action packet families.
// Validates and either replies immediately (NameInvalid, NoCharacterSelected,
// ListCharacters), hits Postgres (CreateCharacter), or pushes a sim-thread
// command (Select/Deselect/Start/Cancel/Disconnect) for state-mutating work.
//
// Lives alongside chat_service / auth_service. Holds non-owning references to
// everything it touches; main.cpp owns the lifetimes.
class CharacterService {
public:
    using Sender = std::function<void(net::ConnId, const uint8_t*, size_t)>;

    CharacterService(session::SessionManager& sessions,
                     CharacterStore& store,
                     sim::SimLoop& sim_loop,
                     net::ITransport& transport,
                     LocationId spawn_location_id,
                     Sender private_sender);

    // ── Character lifecycle (world.fbs) ──────────────────────────────────
    void handle_create_character(net::ConnId conn, const ::world::CreateCharacter& pkt);
    void handle_list_characters(net::ConnId conn, const ::world::ListCharacters& pkt);
    void handle_select_character(net::ConnId conn, const ::world::SelectCharacter& pkt);
    void handle_deselect_character(net::ConnId conn, const ::world::DeselectCharacter& pkt);

    // ── Actions (action.fbs) ─────────────────────────────────────────────
    void handle_start_action(net::ConnId conn, const ::action::StartAction& pkt);
    void handle_cancel_action(net::ConnId conn, const ::action::CancelAction& pkt);

    // Called by main.cpp's transport.on_disconnect on the IO thread. Pushes
    // CharacterDisconnectCommand if the session has a character bound, so the
    // sim thread can clean up location membership and broadcast CharacterLeft.
    // The session's removal from SessionManager is the caller's job.
    void handle_disconnect(net::ConnId conn);

private:
    void send_character_rejected(net::ConnId conn, uint8_t reason);
    void send_action_rejected(net::ConnId conn, uint8_t reason);

    session::SessionManager& sessions_;
    CharacterStore& store_;
    sim::SimLoop& sim_loop_;
    net::ITransport& transport_;
    LocationId spawn_location_id_;
    Sender private_sender_;
};

} // namespace game::world
