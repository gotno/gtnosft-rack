#pragma once

#include "plugin.hpp"

#include "MessagePacker.hpp"

class ChunkedSend;

struct ChunkPacker : MessagePacker {
  ChunkPacker(int32_t _chunkNum, ChunkedSend* _chunkedSend);
  ~ChunkPacker() override;

  int32_t chunkNum{0};
  ChunkedSend* chunkedSend{NULL};

  void pack(osc::OutboundPacketStream& message) override;
};
