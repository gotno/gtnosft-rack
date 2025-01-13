#pragma once

#include <string>
#include "../../../dep/oscpack/osc/OscOutboundPacketStream.h"

struct MessagePacker {
  MessagePacker();
  virtual ~MessagePacker();
  std::string path{""};
  int32_t postSendDelay{0};
  virtual void pack(osc::OutboundPacketStream& message);
  virtual bool isNoop() { return false; }
  virtual void finish() {}
};
