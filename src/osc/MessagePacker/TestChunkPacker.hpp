#pragma once

#include "plugin.hpp"

#include "MessagePacker.hpp"

class ChunkedTest;

struct TestChunkPacker : MessagePacker {
  TestChunkPacker(int32_t chunkNum, ChunkedTest* _chunkedTest);
  ~TestChunkPacker() override;

  int32_t chunkNum{0};
  ChunkedTest* chunkedTest{NULL};

  void pack(osc::OutboundPacketStream& message) override;
};
