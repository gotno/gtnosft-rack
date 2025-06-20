#include "ChunkedImageBundler.hpp"
#include "../ChunkedSend/ChunkedImage.hpp"

ChunkedImageBundler::ChunkedImageBundler(
  int32_t chunkedSendId,
  int32_t chunkNum,
  int32_t numChunks,
  int32_t chunkSize,
  int64_t totalSize,
  uint8_t* data,
  int32_t thisChunkSize,
  int32_t _width,
  int32_t _height
): Bundler("ChunkedImageBundler"),
  ChunkedSendBundler(
    "/set/texture",
    chunkedSendId,
    chunkNum,
    numChunks,
    chunkSize,
    totalSize,
    data,
    thisChunkSize
  ),
  width(_width),
  height(_height) {}

void ChunkedImageBundler::bundleMetadata(osc::OutboundPacketStream& pstream) {
  ChunkedSendBundler::bundleMetadata(pstream);

  pstream << width
    << height
    ;
}
