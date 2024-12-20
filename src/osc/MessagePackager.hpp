#include "../../dep/oscpack/osc/OscOutboundPacketStream.h"

struct MessagePacker {
  MessagePacker();
  ~MessagePacker();
  virtual void process(osc::OutboundPacketStream& message);
};
