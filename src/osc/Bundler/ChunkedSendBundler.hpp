#pragma once

#include "Bundler.hpp"

class ChunkedSend;
class ChunkedManager;

struct ChunkedSendBundler : virtual Bundler {
  ChunkedSendBundler(
    int32_t chunkNum,
    int32_t chunkedSendId,
    ChunkedManager* chunkman
  );

  int32_t chunkNum;
  int32_t chunkedSendId;
  ChunkedManager* chunkman;

  ChunkedSend* getChunkedSend();
  size_t getChunkSize(osc::OutboundPacketStream& pstream);

  bool isNoop() override;
  void finish() override;

  virtual void bundleMetadata(
    osc::OutboundPacketStream& pstream,
    ChunkedSend* chunkedSend
  );
};
