#include "ChunkedSend.hpp"

#include "../MessagePacker/MessagePacker.hpp"

ChunkedSend::ChunkedSend(uint8_t* _data, size_t _size):
  data(_data), size(_size) {
    id = ++idCounter;
  }

ChunkedSend::~ChunkedSend() {
  logCompletionDuration();
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
  // logCompletionDuration(chunkNum);
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

void ChunkedSend::logCompletionDuration(int32_t chunkNum) {
  INFO(
    "chunked send %d chunk %d ack'd in %lld microseconds",
    id,
    chunkNum,
    getChunkStatus(chunkNum).getRoundTripDuration()
  );
}

void ChunkedSend::logCompletionDuration() {
  if (sendFailed()) return;

  auto firstSend = chunkStatuses.at(0).firstSendTime;
  auto lastAck = chunkStatuses.at(0).ackTime;

  for (auto& pair : chunkStatuses)
    if (pair.second.ackTime > lastAck)
      lastAck = pair.second.ackTime;

  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
    lastAck - firstSend
  ).count();

  INFO("chunked send %d total round trip %lld microseconds", id, duration);
}

bool ChunkedSend::sendFailed() {
  for (auto& pair : chunkStatuses)
    if (pair.second.sends >= MAX_SENDS)
      return true;

  return false;
}
