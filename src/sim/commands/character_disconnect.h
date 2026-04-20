#pragma once

#include "net/transport.h"
#include "sim/command.h"
#include "world/ids.h"

namespace game::sim {

// Sim-thread command pushed by the IO transport's on_disconnect callback for
// any session that had a character bound. Cancels any active action, emits
// CharacterLeft to the character's last location, and removes the character
// from that location's set. The DB row stays — the character reappears at the
// same location on a subsequent SelectCharacter.
//
// We carry the character_id explicitly rather than looking it up via Session
// because the IO thread typically removes the session from SessionManager
// before this command runs.
class CharacterDisconnectCommand : public Command {
public:
    CharacterDisconnectCommand(net::ConnId conn, world::CharacterId char_id)
        : conn_(conn), char_id_(char_id) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "CharacterDisconnect"; }

private:
    net::ConnId conn_;
    world::CharacterId char_id_;
};

} // namespace game::sim
