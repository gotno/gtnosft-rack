#include "ChunkedSend.hpp"

#include "../MessagePacker/MessagePacker.hpp"

ChunkedSend::ChunkedSend(uint8_t* _data, size_t _size):
  data(_data), size(_size) {
    id = ++idCounter;
  }

ChunkedSend::~ChunkedSend() {
  delete[] data;
}

void ChunkedSend::init() {
  // integer ceiling
  numChunks = (size + chunkSize - 1) / chunkSize;
  for (int32_t i = 0; i < numChunks; i++)
    chunkStatuses.emplace(i, ChunkStatus(i));
}

void ChunkedSend::ack(int32_t chunkNum) {
  getChunkStatus(chunkNum).registerAcked();
}

void ChunkedSend::getUnackedChunkNums(std::vector<int32_t>& chunkNums) {
  for (auto& pair : chunkStatuses)
    if (!pair.second.acked) chunkNums.push_back(pair.first);
}

void ChunkedSend::registerChunkSent(int32_t chunkNum) {
  getChunkStatus(chunkNum).registerSend();
}

ChunkStatus& ChunkedSend::getChunkStatus(int32_t chunkNum) {
  assert(chunkStatuses.count(chunkNum) != 0);
  return chunkStatuses.at(chunkNum);
}

MessagePacker* ChunkedSend::getPackerForChunk(int32_t chunkNum) {
  return new MessagePacker();
}
