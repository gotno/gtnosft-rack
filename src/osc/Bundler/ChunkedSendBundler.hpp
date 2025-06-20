#pragma once

#include "Bundler.hpp"

class ChunkedSend;
class ChunkedManager;

struct ChunkedSendBundler : virtual Bundler {
  ChunkedSendBundler(
    std::string address,
    int32_t chunkedSendId,
    int32_t chunkNum,
    int32_t numChunks,
    int32_t chunkSize,
    int64_t totalSize,
    uint8_t* data,
    int32_t thisChunkSize
  );

  std::string address;
  int32_t chunkedSendId;
  int32_t chunkNum;
  int32_t numChunks;
  int32_t chunkSize;
  int64_t totalSize;

  // build a metadata-only message and see what's left
  size_t getAvailableBundleSpace();

  virtual void bundleMetadata(osc::OutboundPacketStream& pstream);
};
