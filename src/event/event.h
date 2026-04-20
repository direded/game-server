#pragma once

#include "net/framing.h"
#include "net/transport.h"

#include <cstdint>
#include <vector>

namespace game::event {

// Routing hint on an Event. The dispatcher reads this to decide which
// connections receive the payload.
enum class EventScope : uint8_t {
    Global,   // every Authenticated session
    Local,    // co-located sessions (step 007 adds real location filtering;
              // for now same as Global)
    Private,  // single target connection
};

// Location id — placeholder for step 007's real Location/Vision model. Kept
// on Event so call-sites that already know their location can populate it
// today without another breaking change later.
using LocationId = uint32_t;

// A wire-ready broadcast. `payload` is already wire-framed
// (`[packet_id LE][flatbuffer bytes]`), so the dispatcher's send path is a
// raw memcpy-and-forward — no FlatBuffers logic at flush time.
//
// `packet_id` duplicates the first 4 bytes of `payload`; we keep it as a
// named field so event logs stay human-readable without re-decoding the
// frame header.
struct Event {
    EventScope scope = EventScope::Global;
    net::ConnId private_target = 0;  // used only when scope == Private
    LocationId local_location = 0;   // used only when scope == Local
    net::PacketId packet_id = 0;
    std::vector<uint8_t> payload;
};

} // namespace game::event
