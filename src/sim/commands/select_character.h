#pragma once

#include "net/transport.h"
#include "sim/command.h"
#include "world/ids.h"

namespace game::net { class ITransport; }
namespace game::session { class SessionManager; }

namespace game::sim {

// Sim-thread command for `world.SelectCharacter`. Validates ownership against
// the session's account, binds the character to the session, places it in its
// location, and emits CharacterEntered (Local) + a private CharacterSelected.
//
// If the same character is already bound to another session, that session is
// kicked (transport.disconnect). The transport ref is non-owning; main.cpp
// owns it and outlives the sim loop.
class SelectCharacterCommand : public Command {
public:
    SelectCharacterCommand(net::ConnId conn, world::CharacterId char_id,
                           session::SessionManager& sessions,
                           net::ITransport& transport)
        : conn_(conn), char_id_(char_id),
          sessions_(sessions), transport_(transport) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "SelectCharacter"; }

private:
    net::ConnId conn_;
    world::CharacterId char_id_;
    session::SessionManager& sessions_;
    net::ITransport& transport_;
};

} // namespace game::sim
