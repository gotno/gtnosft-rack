#include "DirectHeartbeatBundler.hpp"

DirectHeartbeatBundler::DirectHeartbeatBundler(): Bundler("DirectHeartbeatBundler") {
  double meterAverage = APP->engine->getMeterAverage();
  std::string avg = rack::string::f("%.1f%%",  meterAverage * 100);

  double meterMax = APP->engine->getMeterMax();
  std::string max = rack::string::f("%.1f%%", meterMax * 100);

  messages.emplace_back(
    "/heartbeat",
    [avg, max](osc::OutboundPacketStream& pstream) {
      pstream << avg.c_str() << max.c_str();
    }
  );
}
