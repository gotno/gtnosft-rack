#include "BasicHeartbeatPacker.hpp"

BasicHeartbeatPacker::BasicHeartbeatPacker() {
  path = "/announce";
}

void BasicHeartbeatPacker::pack(osc::OutboundPacketStream& message) {
  INFO("BEAT");
}
