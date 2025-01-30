#pragma once

#include "ChunkPacker.hpp"

class ChunkedImage;

struct ImageChunkPacker : ChunkPacker {
  ImageChunkPacker(int32_t chunkNum, ChunkedImage* _chunkedImage);

  ChunkedImage* chunkedImage{NULL};

  void pack(osc::OutboundPacketStream& message) override;
};
