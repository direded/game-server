#pragma once

#include "net/transport.h"
#include "sim/command.h"

#include <cstdint>

namespace game::sim {

// ClientPing → ServerPong round-trip. The simplest command on the sim path;
// exists only to prove the command-queue / event-dispatcher wiring before
// step-007 introduces real gameplay commands.
class PingCommand : public Command {
public:
    PingCommand(net::ConnId conn_id, uint32_t nonce)
        : conn_id_(conn_id), nonce_(nonce) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "Ping"; }

private:
    net::ConnId conn_id_;
    uint32_t nonce_;
};

} // namespace game::sim
