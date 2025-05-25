#pragma once

#include "ChunkedSendBundler.hpp"

class ChunkedImage;
class ChunkedManager;

struct ChunkedImageBundler : ChunkedSendBundler {
  ChunkedImageBundler(
    int32_t chunkNum,
    int32_t chunkedImageId,
    ChunkedManager* chunkman
  );

  void bundleMetadata(
    osc::OutboundPacketStream& pstream,
    ChunkedSend* chunkedSend
  ) override;
};
