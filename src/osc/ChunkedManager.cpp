#include "ChunkedManager.hpp"

#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
#include "Bundler/ChunkedSendBundler.hpp"

#include <vector>

ChunkedManager::ChunkedManager(OscSender* sender) : osctx(sender) {
  prepWorker = std::thread(&ChunkedManager::runPrepWorker, this);
}

ChunkedManager::~ChunkedManager() {
  stopWorker();
}

void ChunkedManager::add(ChunkedSend* chunked) {
  std::shared_ptr<ChunkedSend> chunkedSend(chunked);

  if (!prepOnWorker) {
    prepare(chunkedSend);
    return;
  }

  {
    std::lock_guard<std::mutex> locker(prepMutex);
    if (prepStopping) return;
    prepQueue.push_back(std::move(chunkedSend));
  }
  prepCondition.notify_one();
}

void ChunkedManager::stopWorker() {
  std::deque<std::shared_ptr<ChunkedSend>> dropped;
  {
    std::lock_guard<std::mutex> locker(prepMutex);
    prepStopping = true;
    std::swap(dropped, prepQueue);
  }
  prepCondition.notify_one();
  if (prepWorker.joinable()) prepWorker.join();
}

void ChunkedManager::runPrepWorker() {
  while (true) {
    std::shared_ptr<ChunkedSend> chunkedSend;
    {
      std::unique_lock<std::mutex> locker(prepMutex);
      prepCondition.wait(locker, [this]() {
        return prepStopping || !prepQueue.empty();
      });
      if (prepStopping) return;
      chunkedSend = std::move(prepQueue.front());
      prepQueue.pop_front();
    }
    prepare(chunkedSend);
  }
}

void ChunkedManager::prepare(const std::shared_ptr<ChunkedSend>& chunkedSend) {
  ChunkedKey key(chunkedSend->id, chunkedSend->sequenceId);
  {
    std::lock_guard<std::mutex> locker(chunkedSendsMutex);
    if (chunkedSends.count(key) != 0) return;
  }

  // init outside the lock so ack() and tick() aren't held up
  chunkedSend->init();

  std::lock_guard<std::mutex> locker(chunkedSendsMutex);
  if (chunkedSends.count(key) != 0) return;
  chunkedSends.emplace(key, chunkedSend);

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
      "chunked send %lld-%d failed: chunk exceeded %d retries",
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
