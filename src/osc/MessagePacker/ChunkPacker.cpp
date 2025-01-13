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

bool ChunkPacker::isNoop() {
  // send already finished, or this chunk already ack'd
  return !chunkedSend || chunkedSend->chunkAckTimes.count(chunkNum);
}

void ChunkPacker::finish() {
  chunkedSend->registerChunkSent(chunkNum);
}

void ChunkPacker::pack(osc::OutboundPacketStream& message) {
  const int32_t chunkSize =
    chunkNum == chunkedSend->numChunks - 1
      ? chunkedSend->size - (chunkedSend->numChunks - 1) * chunkedSend->chunkSize
      : chunkedSend->chunkSize;

  osc::Blob chunk(chunkedSend->data + chunkSize * chunkNum, chunkSize);

  message << chunkedSend->id
    << chunkNum
    << chunkedSend->numChunks
    << chunkSize
    << chunk
    ;

  chunkedSend->registerChunkSent(chunkNum);
}
