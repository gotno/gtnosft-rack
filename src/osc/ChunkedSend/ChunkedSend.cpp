#include "ChunkedSend.hpp"

#include "../Bundler/ChunkedSendBundler.hpp"

#include <algorithm>

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

bool ChunkedSend::ack(int32_t chunkNum, std::chrono::nanoseconds* rtt) {
  std::lock_guard<std::mutex> locker(statusMutex);
  if (!validChunk(chunkNum)) return false;

  ChunkStatus& chunk = chunks[chunkNum];
  if (chunk.state == ChunkState::Acked) return false;

  auto now = clock::now();
  // Queued with sendCount 1 may mean a resend is mid-send: ambiguous
  if (chunk.state == ChunkState::Sent && chunk.sendCount == 1)
    maxRtt = std::max(maxRtt, std::chrono::nanoseconds(now - chunk.lastSentAt));

  chunk.state = ChunkState::Acked;
  chunk.ackedAt = now;
  ++numAcked;
  if (rtt && numAcked == numChunks && maxRtt.count() > 0) *rtt = maxRtt;
  BENCH(
    if (trace && numAcked == numChunks) trace->stamp(bench::Stage::AllAcked);
  )
  return true;
}

bool ChunkedSend::acked(int32_t chunkNum) {
  std::lock_guard<std::mutex> locker(statusMutex);
  return validChunk(chunkNum) && chunks[chunkNum].state == ChunkState::Acked;
}

int32_t ChunkedSend::takeChunksDue(
  std::vector<int32_t>& chunkNums,
  bool adaptive,
  std::chrono::nanoseconds rto,
  std::chrono::nanoseconds maxRto
) {
  std::lock_guard<std::mutex> locker(statusMutex);
  auto now = clock::now();
  int32_t firstTimeouts = 0;

  for (int32_t chunkNum = 0; chunkNum < (int32_t)chunks.size(); ++chunkNum) {
    ChunkStatus& chunk = chunks[chunkNum];

    if (chunk.state == ChunkState::Sent) {
      if (adaptive) {
        // exponential backoff per resend, so a stalled link isn't flooded
        int doublings = std::min<int>(chunk.sendCount - 1, 5);
        auto timeout = std::min(rto * (1 << doublings), maxRto);
        if (now - chunk.lastSentAt < timeout) continue;
        if (now - chunk.firstSentAt >= GIVE_UP_AFTER) {
          failed = true;
          return firstTimeouts;
        }
        if (chunk.sendCount == 1) ++firstTimeouts;
      } else {
        if (now - chunk.lastSentAt < RETRY_TIMEOUT) continue;
        if (chunk.sendCount > MAX_RETRIES) {
          failed = true;
          return firstTimeouts;
        }
      }
    } else if (chunk.state != ChunkState::Pending) {
      continue;
    }

    chunk.state = ChunkState::Queued;
    chunkNums.push_back(chunkNum);
  }

  return firstTimeouts;
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
