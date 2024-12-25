#include "MessagePacker.hpp"

struct EchoPacker : MessagePacker {
  EchoPacker(std::string _toEcho);
  std::string toEcho{""};
  void pack(osc::OutboundPacketStream& message) override;
};
