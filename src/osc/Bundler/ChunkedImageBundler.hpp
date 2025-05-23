#pragma once

#include "ChunkedSendBundler.hpp"

class ChunkedImage;

struct ChunkedImageBundler : ChunkedSendBundler {
  ChunkedImageBundler(
    int32_t chunkNum,
    std::shared_ptr<ChunkedImage> chunkedImage
  );

  std::shared_ptr<ChunkedImage> chunkedImage;
  void bundleMetadata(osc::OutboundPacketStream& pstream) override;
};
