#include "ChunkedSend.hpp"

#include "../Bundler/ChunkedSendBundler.hpp"

ChunkedSend::ChunkedSend(uint8_t* _rawPixelData, int64_t _size):
  id(idCounter++),
  pixelData(_rawPixelData, std::default_delete<uint8_t[]>()),
  size(_size) {}

void ChunkedSend::findChunkSize() {
  char* msgBuffer = new char[MSG_BUFFER_SIZE];
  osc::OutboundPacketStream pstream(msgBuffer, MSG_BUFFER_SIZE);
  pstream << osc::BeginBundleImmediate;

  // the bundler will report space remaining after metadata is added
  ChunkedSendBundler* bundler = getBundlerForChunk(0);
  // Assuming getBundlerForChunk throws on failure to create, or never returns null.
  int32_t determinedChunkSize = bundler->getChunkSize(pstream);
  delete bundler; // Delete the bundler to prevent memory leak
  setChunkSize(determinedChunkSize);

  // If std::bad_weak_ptr exceptions persist even after fixing the bundler memory leak,
  // consider investigating ChunkedImage::compressData() and its underlying QOI image compression.
  // Memory corruption in that stage could potentially damage std::shared_ptr control blocks
  // or std::enable_shared_from_this internal state, leading to such exceptions.
  delete[] msgBuffer;
}

ChunkedSend::~ChunkedSend() {
  // logCompletionDuration();
  // pixelData's managed memory is automatically deallocated by the shared_ptr
}

void ChunkedSend::setChunkSize(int32_t _chunkSize) {
  chunkSize = _chunkSize;
  calculateNumChunks();
}

void ChunkedSend::calculateNumChunks() {
  // integer ceiling
  numChunks = (size + chunkSize - 1) / chunkSize;
}

int32_t ChunkedSend::getSizeOfChunk(int32_t chunkNum) {
  return chunkNum == numChunks - 1
    ? size - (numChunks - 1) * chunkSize
    : chunkSize;
}

osc::Blob ChunkedSend::getBlobForChunk(int32_t chunkNum) {
  const int32_t offset = chunkSize * chunkNum;
  return osc::Blob(pixelData.get() + offset, getSizeOfChunk(chunkNum));
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
