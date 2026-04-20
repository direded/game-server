#pragma once

#include "net/transport.h"

#include <cstdint>

namespace sim {
struct ClientPing;
}

namespace game::sim {

class SimLoop;

// Thin adapter: receives ClientPing on the IO thread and translates it into
// a PingCommand that the sim loop will execute on its next tick. Lives here
// (rather than in main.cpp) so the translation can be unit-tested and so
// future sim-path packets can be registered in one place.
class SimDispatcher {
public:
    explicit SimDispatcher(SimLoop& sim_loop) : sim_loop_(sim_loop) {}

    void handle_client_ping(net::ConnId conn, const ::sim::ClientPing& pkt);

private:
    SimLoop& sim_loop_;
};

} // namespace game::sim
