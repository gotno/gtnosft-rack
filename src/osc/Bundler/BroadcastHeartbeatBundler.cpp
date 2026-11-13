#include "BroadcastHeartbeatBundler.hpp"
#include "../OscReceiver.hpp"
#include "../OscConstants.hpp"

#include "patch.hpp"

BroadcastHeartbeatBundler::BroadcastHeartbeatBundler(
  int64_t ctrlId
): Bundler("BroadcastHeartbeatBundler") {
  std::string filename = rack::system::getFilename(APP->patch->path);

  std::string pluginVersion;
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == "gotnosoft") {
      pluginVersion = plugin->version;
      break;
    }
  }


  messages.emplace_back(
    "/announce",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << OscReceiver::activePort
        << HEARTBEAT_INTERVAL_MS
        << pluginVersion.c_str()
        << ctrlId
        << filename.c_str()
        ;
    }
  );
}
