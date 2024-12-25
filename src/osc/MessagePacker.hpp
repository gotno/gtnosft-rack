#include <string>
#include "../../dep/oscpack/osc/OscOutboundPacketStream.h"

struct MessagePacker {
  MessagePacker();
  virtual ~MessagePacker();
  std::string path{""};
  virtual void pack(osc::OutboundPacketStream& message);
};
