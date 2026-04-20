#pragma once

#include "net/transport.h"
#include "sim/command.h"

namespace game::session { class SessionManager; }

namespace game::sim {

// Sim-thread command for `world.DeselectCharacter`. Reverse of SelectCharacter:
// emits CharacterLeft (Local), removes the character from its location's set,
// unbinds the session, and replies with a private CharacterDeselected.
//
// If the character had a current_action, it is silently cleared (no
// ActionCancelled emit — Deselect is a stronger signal).
class DeselectCharacterCommand : public Command {
public:
    DeselectCharacterCommand(net::ConnId conn, session::SessionManager& sessions)
        : conn_(conn), sessions_(sessions) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "DeselectCharacter"; }

private:
    net::ConnId conn_;
    session::SessionManager& sessions_;
};

} // namespace game::sim
