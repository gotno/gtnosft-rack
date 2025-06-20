#pragma once

#include "ChunkedSendBundler.hpp"

class ChunkedImage;
class ChunkedManager;

struct ChunkedImageBundler : ChunkedSendBundler {
  ChunkedImageBundler(
    int32_t chunkedSendId,
    int32_t chunkNum,
    int32_t numChunks,
    int32_t chunkSize,
    int64_t totalSize,
    uint8_t* data,
    int32_t thisChunkSize,
    int32_t width,
    int32_t height
  );

  int32_t width;
  int32_t height;

  void bundleMetadata(osc::OutboundPacketStream& pstream) override;
};
