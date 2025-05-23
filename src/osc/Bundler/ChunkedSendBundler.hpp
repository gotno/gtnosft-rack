#pragma once

#include "Bundler.hpp"

class ChunkedSend;

struct ChunkedSendBundler : virtual Bundler {
  ChunkedSendBundler(int32_t chunkNum, ChunkedSend* chunkedSend);

  int32_t chunkNum;
  ChunkedSend* chunkedSend{NULL};

  void finish() override;

  virtual void bundleMetadata(osc::OutboundPacketStream& pstream);
  void resetBundle(osc::OutboundPacketStream& pstream);
};
