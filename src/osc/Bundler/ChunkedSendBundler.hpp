#pragma once

#include "Bundler.hpp"

#include <memory>
#include <functional> // Required for std::function

class ChunkedSend; // Forward declaration, though ChunkedSend object is no longer a member

struct ChunkedSendBundler : virtual Bundler {
  // Constructor updated to accept necessary data directly
  ChunkedSendBundler(
    int32_t chunkNum,
    int32_t id,
    int32_t numChunks,
    int32_t chunkSize,
    int64_t totalSize,
    std::shared_ptr<uint8_t[]> data_ptr, // Changed from ChunkedSend pointer
    bool isNoopStatus,
    std::function<void(int32_t)> regChunkSentCb
  );

  int32_t chunkNum; // Current chunk number being processed

  // Members to store data previously fetched from ChunkedSend
  int32_t sendIdM;
  int32_t numChunksM;
  int32_t chunkSizeM; // The general chunk size
  int64_t totalSizeM;
  std::shared_ptr<uint8_t[]> pixelDataM; // Holds the pixel data
  bool isNoopM;                          // Pre-calculated isNoop status
  std::function<void(int32_t)> registerChunkSentCbM; // Callback for registration

  bool isNoop() override;
  void finish() override;

  virtual void bundleMetadata(osc::OutboundPacketStream& pstream);

  size_t getChunkSize(osc::OutboundPacketStream& pstream);
};
