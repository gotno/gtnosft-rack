#include "MessagePacker.hpp"

struct DirectHeartbeatPacker : MessagePacker {
  DirectHeartbeatPacker();

  double meterAverage, meterMax;

  void pack(osc::OutboundPacketStream& message) override;
};
