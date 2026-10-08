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

  std::lock_guard<std::mutex> locker(statusMutex);
  chunks.assign(numChunks, ChunkStatus());
  numAcked = 0;
  BENCH(if (trace) trace->numChunks = numChunks;)
}

ChunkedSend::~ChunkedSend() {
  // logCompletionDuration();
  BENCH(
    if (trace) {
      if (!sendSucceeded()) trace->failed = true;
      bench::submit(*trace);
    }
  )
  delete[] data;
}

bool ChunkedSend::validChunk(int32_t chunkNum) {
  return chunkNum >= 0 && chunkNum < (int32_t)chunks.size();
}

void ChunkedSend::ack(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  if (!validChunk(chunkNum)) return;

  ChunkStatus& chunk = chunks[chunkNum];
  if (chunk.state == ChunkState::Acked) return;

  chunk.state = ChunkState::Acked;
  chunk.ackedAt = clock::now();
  ++numAcked;
  BENCH(
    if (trace && numAcked == numChunks) trace->stamp(bench::Stage::AllAcked);
  )
}

bool ChunkedSend::acked(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  return validChunk(chunkNum) && chunks[chunkNum].state == ChunkState::Acked;
}

void ChunkedSend::takeChunksDue(std::vector<int32_t>& chunkNums) {
  std::lock_guard<std::mutex> locker(statusMutex);

  for (int32_t chunkNum = 0; chunkNum < (int32_t)chunks.size(); ++chunkNum) {
    ChunkStatus& chunk = chunks[chunkNum];

    if (chunk.state == ChunkState::Sent) {
      if (clock::now() - chunk.lastSentAt < RETRY_TIMEOUT) continue;
      if (chunk.sendCount > MAX_RETRIES) {
        failed = true;
        return;
      }
    } else if (chunk.state != ChunkState::Pending) {
      continue;
    }

    chunk.state = ChunkState::Queued;
    chunkNums.push_back(chunkNum);
  }
}

void ChunkedSend::registerChunkSent(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  if (!validChunk(chunkNum)) return;

  ChunkStatus& chunk = chunks[chunkNum];
  auto now = clock::now();
  if (chunk.sendCount == 0) chunk.firstSentAt = now;
  chunk.lastSentAt = now;
  BENCH(
    if (trace) {
      if (chunk.sendCount == 0) {
        if (trace->chunksSent == 0) trace->stamp(bench::Stage::FirstChunkSent);
        if (++trace->chunksSent == numChunks)
          trace->stamp(bench::Stage::LastChunkSent);
      } else {
        ++trace->retries;
      }
    }
  )
  if (chunk.sendCount < UINT8_MAX) ++chunk.sendCount;

  // an ack for an earlier send may have arrived while this one was queued
  if (chunk.state != ChunkState::Acked) chunk.state = ChunkState::Sent;
}

void ChunkedSend::registerChunkDropped(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  if (!validChunk(chunkNum)) return;

  if (chunks[chunkNum].state == ChunkState::Queued)
    chunks[chunkNum].state = ChunkState::Pending;
}

void ChunkedSend::logCompletionDuration(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  if (!validChunk(chunkNum)) return;

  const ChunkStatus& chunk = chunks[chunkNum];
  if (chunk.state != ChunkState::Acked || chunk.sendCount == 0) return;

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    chunk.ackedAt - chunk.lastSentAt
  ).count();

  INFO(
    "chunked send %lld chunk %d ack'd in %lld milliseconds",
    (long long)id,
    chunkNum,
    (long long)duration
  );
}

void ChunkedSend::logCompletionDuration() {
  if (sendFailed() || !sendSucceeded()) return;

  std::lock_guard<std::mutex> locker(statusMutex);
  if (chunks.empty()) return;

  time_point firstSend = chunks[0].firstSentAt;
  time_point lastAck = chunks[0].ackedAt;

  for (const ChunkStatus& chunk : chunks) {
    if (chunk.sendCount > 0 && chunk.firstSentAt < firstSend)
      firstSend = chunk.firstSentAt;
    if (chunk.ackedAt > lastAck) lastAck = chunk.ackedAt;
  }

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
    lastAck - firstSend
  ).count();

  INFO(
    "chunked send %lld total round trip %lld milliseconds",
    (long long)id,
    (long long)duration
  );
}

bool ChunkedSend::sendSucceeded() {
  std::lock_guard<std::mutex> locker(statusMutex);
  return numAcked == numChunks;
}

bool ChunkedSend::sendFailed() {
  return failed;
}
