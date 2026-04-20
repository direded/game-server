#pragma once

#include "net/transport.h"
#include "sim/command.h"
#include "world/action.h"
#include "world/ids.h"

#include <variant>

namespace game::sim {

// Sim-thread command for `action.StartAction`. Built on the IO thread by the
// action handler after basic decode+session checks; preconditions that need
// world state (linked locations, busy character) are checked here.
class StartActionCommand : public Command {
public:
    // Today the only payload is Travel; future kinds extend this variant.
    using Params = std::variant<world::TravelParams>;

    StartActionCommand(net::ConnId conn, world::CharacterId char_id,
                       world::ActionKind kind, Params params)
        : conn_(conn), char_id_(char_id), kind_(kind),
          params_(std::move(params)) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "StartAction"; }

private:
    net::ConnId conn_;
    world::CharacterId char_id_;
    world::ActionKind kind_;
    Params params_;
};

} // namespace game::sim
