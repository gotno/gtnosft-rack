#include "ChunkedTest.hpp"

#include "../MessagePacker/TestChunkPacker.hpp"

ChunkedTest::ChunkedTest(uint8_t* _data, size_t _size):
  ChunkedSend(_data, _size) {
    chunkSize = 10;
    calculateNumChunks();
  }

MessagePacker* ChunkedTest::getPackerForChunk(int32_t chunkNum) {
  return new TestChunkPacker(chunkNum, this);
}
