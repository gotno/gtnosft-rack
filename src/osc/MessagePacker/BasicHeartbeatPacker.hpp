#include <rack.hpp>

#include "MessagePacker.hpp"

struct BasicHeartbeatPacker : MessagePacker {
  BasicHeartbeatPacker();

  void pack(osc::OutboundPacketStream& message) override;
};
