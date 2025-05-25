#pragma once

#include "ChunkedSendBundler.hpp" // Provides std::shared_ptr, std::function

// class ChunkedImage; // Forward declaration no longer needed as no direct member

struct ChunkedImageBundler : ChunkedSendBundler {
  ChunkedImageBundler(
    int32_t chunkNum,
    // Params for ChunkedSendBundler base
    int32_t id,
    int32_t numChunks,
    int32_t chunkSize,
    int64_t totalSize,
    std::shared_ptr<uint8_t[]> data_ptr,
    bool isNoopStatus,
    std::function<void(int32_t)> regChunkSentCb,
    // Params for ChunkedImageBundler itself
    int32_t imgWidth,
    int32_t imgHeight
  );

  int32_t imageWidthM;
  int32_t imageHeightM;

  void bundleMetadata(osc::OutboundPacketStream& pstream) override;
};
