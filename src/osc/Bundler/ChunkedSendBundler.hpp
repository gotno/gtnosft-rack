#pragma once

#include "Bundler.hpp"

#include <memory>

class ChunkedSend;

struct ChunkedSendBundler : virtual Bundler {
  ChunkedSendBundler(int32_t chunkNum, std::shared_ptr<ChunkedSend> chunkedSend);

  int32_t chunkNum;
  std::shared_ptr<ChunkedSend> chunkedSend{NULL};

  bool isNoop() override;
  void finish() override;

  virtual void bundleMetadata(osc::OutboundPacketStream& pstream);

  size_t getChunkSize(osc::OutboundPacketStream& pstream);
};
