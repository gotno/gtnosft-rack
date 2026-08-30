#include "BroadcastHeartbeatBundler.hpp"
#include "../OscReceiver.hpp"
#include "../OscConstants.hpp"

#include "patch.hpp"

BroadcastHeartbeatBundler::BroadcastHeartbeatBundler(): Bundler("BroadcastHeartbeatBundler") {
  std::string filename = rack::system::getFilename(APP->patch->path);

  messages.emplace_back(
    "/announce",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << OscReceiver::activePort
        << HEARTBEAT_INTERVAL_MS
        << filename.c_str()
        ;
    }
  );
}
