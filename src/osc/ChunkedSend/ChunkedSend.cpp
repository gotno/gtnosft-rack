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
}

void ChunkedSend::ack(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  chunkAckTimes.insert({chunkNum, std::chrono::steady_clock::now()});
}

void ChunkedSend::getUnackedChunkNums(std::vector<int32_t>& chunkNums) {
  int32_t chunkNum{-1};

  std::lock_guard<std::mutex> locker(statusMutex);

  while(++chunkNum < numChunks && chunkNums.size() < BATCH_SIZE) {
    if (!chunkAckTimes.count(chunkNum))
      chunkNums.push_back(chunkNum);
  }
}

void ChunkedSend::registerChunkSent(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);

  chunkSendTimes.insert({chunkNum, std::chrono::steady_clock::now()});
  auto pair = chunkSendCounts.insert({chunkNum, 0});
  if (!pair.second) ++chunkSendCounts.at(chunkNum);
}

MessagePacker* ChunkedSend::getPackerForChunk(int32_t chunkNum) {
  return new MessagePacker();
}

void ChunkedSend::logCompletionDuration(int32_t chunkNum) {
  if (!chunkAckTimes.count(chunkNum)) return;

  size_t duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    chunkAckTimes.at(chunkNum) - chunkSendTimes.at(chunkNum)
  ).count();

  INFO(
    "chunked send %d chunk %d ack'd in %lld milliseconds",
    id,
    chunkNum,
    duration
  );
}

void ChunkedSend::logCompletionDuration() {
  if (sendFailed()) return;

  auto firstSend = chunkSendTimes.at(0);
  auto lastAck = chunkAckTimes.at(0);

  for (const auto& pair : chunkAckTimes)
    if (pair.second > lastAck)
      lastAck = pair.second;

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    lastAck - firstSend
  ).count();

  INFO("chunked send %d total round trip %lld milliseconds", id, duration);
}

bool ChunkedSend::sendFailed() {
  std::lock_guard<std::mutex> locker(statusMutex);

  for (const auto& pair : chunkSendCounts)
    if (pair.second >= MAX_SENDS)
      return true;

  return false;
}
