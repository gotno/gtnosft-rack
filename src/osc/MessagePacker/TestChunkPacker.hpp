#pragma once

#include "plugin.hpp"

#include "ChunkPacker.hpp"

class ChunkedTest;

struct TestChunkPacker : ChunkPacker {
  TestChunkPacker(int32_t chunkNum, ChunkedTest* _chunkedTest);
};
