#pragma once

#include "rack.hpp"

#include "MessagePacker.hpp"

class ChunkedSend;

struct ChunkPacker : MessagePacker {
  ChunkPacker(int32_t _chunkNum, ChunkedSend* _chunkedSend);
  ~ChunkPacker() override;

  int32_t chunkNum{0};
  ChunkedSend* chunkedSend{NULL};

  bool isNoop() override;
  void finish() override;
  void pack(osc::OutboundPacketStream& message) override;
};
