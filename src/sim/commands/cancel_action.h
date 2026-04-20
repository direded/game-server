#pragma once

#include "net/transport.h"
#include "sim/command.h"
#include "world/ids.h"

namespace game::sim {

// Sim-thread command for `action.CancelAction`. Clears the character's
// current action if any; emits ActionCancelled (Local broadcast) and a
// private copy to the sender. No-op if the character isn't in an action.
class CancelActionCommand : public Command {
public:
    CancelActionCommand(net::ConnId conn, world::CharacterId char_id)
        : conn_(conn), char_id_(char_id) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "CancelAction"; }

private:
    net::ConnId conn_;
    world::CharacterId char_id_;
};

} // namespace game::sim
