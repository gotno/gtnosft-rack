#pragma once

#include "plugin.hpp"

#include "ChunkPacker.hpp"

class ChunkedImage;

struct ImageChunkPacker : ChunkPacker {
  ImageChunkPacker(int32_t chunkNum, ChunkedImage* _chunkedImage);
  ~ImageChunkPacker() override;

  ChunkedImage* chunkedImage{NULL};

  void pack(osc::OutboundPacketStream& message) override;
};
