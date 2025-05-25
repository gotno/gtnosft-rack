#include "ChunkedImageBundler.hpp"
#include "../ChunkedSend/ChunkedImage.hpp"

ChunkedImageBundler::ChunkedImageBundler(
  int32_t chunkNum,
  int32_t _chunkedImageId,
  ChunkedManager* _chunkman
):
  Bundler("ChunkedImageBundler"),
  ChunkedSendBundler(chunkNum, _chunkedImageId, _chunkman) {}

void ChunkedImageBundler::bundleMetadata(
  osc::OutboundPacketStream& pstream,
  ChunkedSend* chunkedSend
) {
  ChunkedSendBundler::bundleMetadata(pstream, chunkedSend);

  ChunkedImage* chunkedImage = dynamic_cast<ChunkedImage*>(chunkedSend);
  pstream << chunkedImage->width
    << chunkedImage->height
    ;
}
