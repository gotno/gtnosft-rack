#include "TestChunkPacker.hpp"

#include "../ChunkedSend/ChunkedTest.hpp"
#include "../../../dep/oscpack/osc/OscTypes.h"

TestChunkPacker::TestChunkPacker(int32_t _chunkNum, ChunkedTest* _chunkedTest):
  chunkNum(_chunkNum), chunkedTest(_chunkedTest) {
    path = "/chunked_test";
  }

TestChunkPacker::~TestChunkPacker() {
  chunkedTest = NULL;
}

void TestChunkPacker::pack(osc::OutboundPacketStream& message) {
  const int32_t& chunkSize = chunkedTest->chunkSize;
  osc::Blob chunk(chunkedTest->data + chunkSize * chunkNum, chunkSize);

  message << chunkedTest->id
    << chunkSize
    << chunkNum
    << chunk
    ;

  chunkedTest->registerChunkSent(chunkNum);
}
