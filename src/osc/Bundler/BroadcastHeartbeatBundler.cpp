#include "BroadcastHeartbeatBundler.hpp"
#include "../OscReceiver.hpp"
#include "../OscConstants.hpp"

BroadcastHeartbeatBundler::BroadcastHeartbeatBundler(): Bundler("BroadcastHeartbeatBundler") {
  messages.emplace_back("/announce", [](osc::OutboundPacketStream& pstream) {
    pstream << OscReceiver::activePort
      << HEARTBEAT_DELAY;
  });
}
