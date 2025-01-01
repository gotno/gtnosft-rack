#include "TestChunkPacker.hpp"

#include "../ChunkedSend/ChunkedTest.hpp"
#include "../../../dep/oscpack/osc/OscTypes.h"

TestChunkPacker::TestChunkPacker(int32_t _chunkNum, ChunkedTest* _chunkedTest):
  ChunkPacker(_chunkNum, _chunkedTest) {
    path = "/chunked_test";
  }
