#include "MessagePacker.hpp"

struct BlobTestPacker : MessagePacker {
  BlobTestPacker();
  void pack(osc::OutboundPacketStream& message) override;
};
