#include "BlobTestPacker.hpp"
#include "../../../dep/oscpack/osc/OscTypes.h"

BlobTestPacker::BlobTestPacker() {
  path = "/blobtest";
}

void BlobTestPacker::pack(osc::OutboundPacketStream& message) {
  uint8_t data[10];
  for (int i = 0; i < 10; i++) data[i] = 100 - i;
  osc::Blob blob(data, 10);
  message << blob;
}
