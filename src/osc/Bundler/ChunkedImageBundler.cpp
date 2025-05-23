#include "ChunkedImageBundler.hpp"
#include "../ChunkedSend/ChunkedImage.hpp"

ChunkedImageBundler::ChunkedImageBundler(
  int32_t chunkNum,
  ChunkedImage* _chunkedImage
):
  Bundler("ChunkedImageBundler"),
  ChunkedSendBundler(chunkNum, _chunkedImage),
  chunkedImage(_chunkedImage) {}

void ChunkedImageBundler::bundleMetadata(osc::OutboundPacketStream& pstream) {
  ChunkedSendBundler::bundleMetadata(pstream);

  pstream << chunkedImage->width
    << chunkedImage->height
    ;
}
