#include "ChunkedImageBundler.hpp"
// #include "../ChunkedSend/ChunkedImage.hpp" // No longer needed

ChunkedImageBundler::ChunkedImageBundler(
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
) : Bundler("ChunkedImageBundler"), // Initialize virtual base
    ChunkedSendBundler(chunkNum, id, numChunks, chunkSize, totalSize, data_ptr, isNoopStatus, regChunkSentCb),
    imageWidthM(imgWidth),
    imageHeightM(imgHeight) {
  // Constructor body is likely empty now, specific messages are handled by ChunkedSendBundler
}

void ChunkedImageBundler::bundleMetadata(osc::OutboundPacketStream& pstream) {
  ChunkedSendBundler::bundleMetadata(pstream); // Call base to bundle common metadata

  pstream << imageWidthM   // Use stored member
    << imageHeightM;  // Use stored member
}
