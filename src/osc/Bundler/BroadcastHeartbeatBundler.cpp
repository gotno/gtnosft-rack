#include "BroadcastHeartbeatBundler.hpp"
#include "../OscConstants.hpp"

BroadcastHeartbeatBundler::BroadcastHeartbeatBundler(): Bundler("BroadcastHeartbeatBundler") {
  messages.emplace_back("/announce", [](osc::OutboundPacketStream& pstream) {
    pstream << RX_PORT
      << HEARTBEAT_DELAY;
  });
}
