#include "ChunkedSend.hpp"

#include "../Bundler/ChunkedSendBundler.hpp"

ChunkedSend::ChunkedSend(uint8_t* _data, int64_t _size):
  id(idCounter++), data(_data), size(_size) {}


void ChunkedSend::init() {
  ChunkedSendBundler* bundler = getBundlerForChunk(0);

  chunkSize = bundler->getAvailableBundleSpace();
  // quick integer ceiling
  numChunks = (size + chunkSize - 1) / chunkSize;

  delete bundler;
}

ChunkedSend::~ChunkedSend() {
  // logCompletionDuration();
  delete[] data;
}

void ChunkedSend::ack(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  chunkAckTimes.insert({chunkNum, std::chrono::steady_clock::now()});
}

bool ChunkedSend::acked(int32_t chunkNum) {
  return chunkAckTimes.count(chunkNum) != 0;
}

void ChunkedSend::getUnackedChunkNums(std::vector<int32_t>& chunkNums) {
  int32_t chunkNum{-1};

  std::lock_guard<std::mutex> locker(statusMutex);

  while(++chunkNum < numChunks)
    if (!acked(chunkNum)) chunkNums.push_back(chunkNum);
}

void ChunkedSend::registerChunkSent(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);

  chunkSendTimes.insert({chunkNum, std::chrono::steady_clock::now()});
  auto pair = chunkSendCounts.insert({chunkNum, 0});
  if (!pair.second) ++chunkSendCounts.at(chunkNum);
  if (chunkSendCounts.at(chunkNum) > MAX_SENDS) failed = true;
}

void ChunkedSend::logCompletionDuration(int32_t chunkNum) {
  if (!acked(chunkNum)) return;

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
