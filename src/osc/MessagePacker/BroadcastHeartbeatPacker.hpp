#include "MessagePacker.hpp"

struct BroadcastHeartbeatPacker : MessagePacker {
  BroadcastHeartbeatPacker();

  void pack(osc::OutboundPacketStream& message) override;
};
