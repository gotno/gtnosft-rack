#include "BroadcastHeartbeatPacker.hpp"
#include "../OscConstants.hpp"

BroadcastHeartbeatPacker::BroadcastHeartbeatPacker() {
  path = "/announce";
}

void BroadcastHeartbeatPacker::pack(osc::OutboundPacketStream& message) {
  message << RX_PORT
    << HEARTBEAT_DELAY;
}
