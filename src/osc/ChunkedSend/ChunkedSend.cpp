#include "ChunkedSend.hpp"

#include "../Bundler/ChunkedSendBundler.hpp"

ChunkedSend::ChunkedSend(uint8_t* _data, int64_t _size):
  id(idCounter++), data(_data), size(_size) {}

ChunkedSend::~ChunkedSend() {
  // logCompletionDuration();
  delete[] data;
}

void ChunkedSend::setChunkSize(int32_t _chunkSize) {
  chunkSize = _chunkSize;
  calculateNumChunks();
}

void ChunkedSend::calculateNumChunks() {
  // integer ceiling
  numChunks = (size + chunkSize - 1) / chunkSize;

  ready = true;
}

int32_t ChunkedSend::getSizeOfChunk(int32_t chunkNum) {
  return chunkNum == numChunks - 1
    ? size - (numChunks - 1) * chunkSize
    : chunkSize;
}

osc::Blob ChunkedSend::getBlobForChunk(int32_t chunkNum) {
  const int32_t offset = chunkSize * chunkNum;
  return osc::Blob(data + offset, getSizeOfChunk(chunkNum));
}

void ChunkedSend::ack(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  chunkAckTimes.insert({chunkNum, std::chrono::steady_clock::now()});
}

void ChunkedSend::getUnackedChunkNums(std::vector<int32_t>& chunkNums) {
  int32_t chunkNum{-1};

  std::lock_guard<std::mutex> locker(statusMutex);

  while(++chunkNum < numChunks) {
    if (!chunkAckTimes.count(chunkNum))
      chunkNums.push_back(chunkNum);
  }
}

void ChunkedSend::registerChunkSent(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);

  chunkSendTimes.insert({chunkNum, std::chrono::steady_clock::now()});
  auto pair = chunkSendCounts.insert({chunkNum, 0});
  if (!pair.second) ++chunkSendCounts.at(chunkNum);
  if (chunkSendCounts.at(chunkNum) > MAX_SENDS) failed = true;
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

bool ChunkedSend::sendSucceeded() {
  std::lock_guard<std::mutex> locker(statusMutex);

  return chunkAckTimes.size() == (uint32_t)numChunks;
}

bool ChunkedSend::sendFailed() {
  return failed;
}
