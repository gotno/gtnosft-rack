#include "rack.hpp"

#include "DirectHeartbeatPacker.hpp"

DirectHeartbeatPacker::DirectHeartbeatPacker() {
  path = "/heartbeat";

  meterAverage = APP->engine->getMeterAverage();
  meterMax = APP->engine->getMeterMax();
}

void DirectHeartbeatPacker::pack(osc::OutboundPacketStream& message) {
  std::string avg = rack::string::f("%.1f%%",  meterAverage * 100);
  std::string max = rack::string::f("%.1f%%", meterMax * 100);

  message << avg.c_str() << max.c_str();
}
