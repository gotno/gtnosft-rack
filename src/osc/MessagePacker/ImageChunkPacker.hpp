#pragma once

#include "plugin.hpp"

#include "MessagePacker.hpp"

class ChunkedImage;

struct ImageChunkPacker : MessagePacker {
  ImageChunkPacker(int32_t chunkNum, ChunkedImage* _chunkedImage);
  ~ImageChunkPacker() override;

  int32_t chunkNum{0};
  ChunkedImage* chunkedImage{NULL};

  void pack(osc::OutboundPacketStream& message) override;
};
