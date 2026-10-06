#include "ChunkedManager.hpp"

#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
#include "Bundler/ChunkedSendBundler.hpp"

#include <vector>

ChunkedManager::ChunkedManager(OscSender* sender) : osctx(sender) {}

ChunkedManager::~ChunkedManager() {}

void ChunkedManager::add(ChunkedSend* chunked) {
  std::shared_ptr<ChunkedSend> chunkedSend(chunked);
  ChunkedKey key(chunkedSend->id, chunkedSend->sequenceId);

  std::lock_guard<std::mutex> locker(chunkedSendsMutex);
  if (chunkedSends.count(key) != 0) return;

  chunkedSend->init();
  chunkedSends.emplace(key, chunkedSend);

  // send immediately rather than waiting for the next tick
  bool validSend = processChunked(chunkedSend);
  if (!validSend) chunkedSends.erase(key);
}

void ChunkedManager::ack(int64_t id, int32_t sequenceId, int32_t chunkNum) {
  std::shared_ptr<ChunkedSend> chunkedSend;
  {
    std::lock_guard<std::mutex> locker(chunkedSendsMutex);
    auto it = chunkedSends.find(ChunkedKey(id, sequenceId));
    if (it == chunkedSends.end()) return;
    chunkedSend = it->second;
  }
  chunkedSend->ack(chunkNum);
}

void ChunkedManager::tick() {
  std::lock_guard<std::mutex> locker(chunkedSendsMutex);

  for (auto it = chunkedSends.begin(); it != chunkedSends.end();) {
    if (processChunked(it->second)) {
      ++it;
    } else {
      it = chunkedSends.erase(it);
    }
  }
}

bool ChunkedManager::processChunked(
  const std::shared_ptr<ChunkedSend>& chunkedSend
) {
  if (chunkedSend->sendFailed() || chunkedSend->sendSucceeded()) return false;

  std::vector<int32_t> dueChunkNums;
  chunkedSend->takeChunksDue(dueChunkNums);

  if (chunkedSend->sendFailed()) {
    WARN(
      "chunked send %lld (sequence %d) failed: chunk exceeded %d retries",
      (long long)chunkedSend->id,
      chunkedSend->sequenceId,
      (int)ChunkedSend::MAX_RETRIES
    );
    return false;
  }

  for (int32_t chunkNum : dueChunkNums) enqueueChunk(chunkedSend, chunkNum);

  return true;
}

void ChunkedManager::enqueueChunk(
  const std::shared_ptr<ChunkedSend>& chunkedSend,
  int32_t chunkNum
) {
  ChunkedSendBundler* bundler = chunkedSend->getBundlerForChunk(chunkNum);

  // bundler callbacks run on the sender thread and must not touch the map
  bundler->noopCheck = [chunkedSend, chunkNum]() {
    return chunkedSend->sendFailed() || chunkedSend->acked(chunkNum);
  };

  bundler->onBundleSent = [chunkedSend, chunkNum]() {
    chunkedSend->registerChunkSent(chunkNum);
  };

  bundler->beforeDestroy = [chunkedSend, chunkNum]() {
    chunkedSend->registerChunkDropped(chunkNum);
  };

  osctx->enqueueBundler(bundler);
}
