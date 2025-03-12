#pragma once

#include <string>
#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscTypes.h"

struct MessagePacker {
  MessagePacker() {}
  virtual ~MessagePacker() {}

  std::string path{""};
  int32_t postSendDelay{0};

  virtual bool isNoop() { return false; }

  virtual void pack(osc::OutboundPacketStream& message) {}
  virtual void finish() {}
};
