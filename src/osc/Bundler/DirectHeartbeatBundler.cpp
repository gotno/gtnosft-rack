#include "DirectHeartbeatBundler.hpp"

DirectHeartbeatBundler::DirectHeartbeatBundler(): Bundler("DirectHeartbeatBundler") {
  float avg = (float)APP->engine->getMeterAverage() * 100;
  float max = (float)APP->engine->getMeterMax() * 100;

  messages.emplace_back(
    "/heartbeat",
    [avg, max](osc::OutboundPacketStream& pstream) {
      pstream << avg << max;
    }
  );
}
