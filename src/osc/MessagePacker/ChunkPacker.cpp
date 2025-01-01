#include "ChunkPacker.hpp"

#include "../ChunkedSend/ChunkedSend.hpp"
#include "../../../dep/oscpack/osc/OscTypes.h"

ChunkPacker::ChunkPacker(int32_t _chunkNum, ChunkedSend* _chunkedSend):
  chunkNum(_chunkNum), chunkedSend(_chunkedSend) {
    path = "/chunked_send";
  }

ChunkPacker::~ChunkPacker() {
  chunkedSend = NULL;
}

void ChunkPacker::pack(osc::OutboundPacketStream& message) {
  const int32_t& chunkSize = chunkedSend->chunkSize;
  osc::Blob chunk(chunkedSend->data + chunkSize * chunkNum, chunkSize);

  message << chunkedSend->id
    << chunkNum
    << chunkedSend->numChunks
    << chunkSize
    << chunk
    ;

  chunkedSend->registerChunkSent(chunkNum);
}
