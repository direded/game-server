#pragma once

#include "net/transport.h"
#include "sim/command.h"
#include "world/character.h"

namespace game::sim {

// Sim-thread command: insert a freshly-created Character into the World
// (already persisted to the DB on the IO thread) and reply to the requesting
// session with `world.CharacterSummary`. Replying from inside the sim thread
// guarantees the client never sees the summary before subsequent sim-side
// commands (e.g. SelectCharacter) can resolve the new id.
class AddCharacterCommand : public Command {
public:
    AddCharacterCommand(net::ConnId conn, world::Character character)
        : conn_(conn), character_(std::move(character)) {}

    void execute(CommandContext& ctx) override;
    const char* name() const override { return "AddCharacter"; }

private:
    net::ConnId conn_;
    world::Character character_;
};

} // namespace game::sim
