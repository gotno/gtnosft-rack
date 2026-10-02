#include "ChunkedManager.hpp"

#include "../OSCctrl.hpp"
#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
#include "Bundler/ChunkedSendBundler.hpp"

#include <thread>
#include <chrono>

ChunkedManager::ChunkedManager(OSCctrlWidget* _ctrl, OscSender* sender)
  : ctrl(_ctrl), osctx(sender) {}

ChunkedManager::~ChunkedManager() {}

void ChunkedManager::add(ChunkedSend* chunked) {
  if (chunkedExists(chunked->id, chunked->sequenceId)) {
    delete chunked;
    return;
  }

  chunked->init();
  chunkedSends.emplace(
    ChunkedKey(chunked->id, chunked->sequenceId),
    std::unique_ptr<ChunkedSend>(chunked)
  );
  processChunked(chunked->id, chunked->sequenceId);
}

void ChunkedManager::ack(int64_t id, int32_t sequenceId, int32_t chunkNum) {
  if (chunkedExists(id, sequenceId)) getChunked(id, sequenceId)->ack(chunkNum);
}

ChunkedSend* ChunkedManager::findChunked(int64_t id, int32_t sequenceId) {
  if (!chunkedExists(id, sequenceId)) return NULL;
  return chunkedSends.at(ChunkedKey(id, sequenceId)).get();
}

bool ChunkedManager::chunkedExists(int64_t id, int32_t sequenceId) {
  return chunkedSends.count(ChunkedKey(id, sequenceId)) != 0;
}

ChunkedSend* ChunkedManager::getChunked(int64_t id, int32_t sequenceId) {
  assert(chunkedExists(id, sequenceId));
  return chunkedSends.at(ChunkedKey(id, sequenceId)).get();
}

void ChunkedManager::processChunked(int64_t id, int32_t sequenceId) {
  if (!chunkedExists(id, sequenceId)) return;
  ChunkedSend* chunkedSend = getChunked(id, sequenceId);

  bool sendFailed = chunkedSend->sendFailed();
  // if (sendFailed) WARN("processing chunked send %d: send failed", id);

  bool sendSucceeded = chunkedSend->sendSucceeded();
  // if (sendSucceeded) INFO("processing chunked send %d: finished", id);

  if (sendFailed || sendSucceeded) {
    chunkedSends.erase(ChunkedKey(id, sequenceId));
    return;
  }

  std::vector<int32_t> unackedChunkNums;
  chunkedSend->getUnackedChunkNums(unackedChunkNums);

  for (int32_t chunkNum : unackedChunkNums) {
    ChunkedSendBundler* bundler =
      chunkedSend->getBundlerForChunk(chunkNum);

    bundler->noopCheck = [this, id, sequenceId, chunkNum](){
      if (!chunkedExists(id, sequenceId)) return true;
      if (getChunked(id, sequenceId)->sendFailed()) return true;
      if (getChunked(id, sequenceId)->acked(chunkNum)) return true;
      return false;
    };

    bundler->onBundleSent = [this, id, sequenceId, chunkNum](){
      if (!chunkedExists(id, sequenceId)) return;
      getChunked(id, sequenceId)->registerChunkSent(chunkNum);
    };

    osctx->enqueueBundler(bundler);
  }

  std::thread([this, id, sequenceId]() {
    // TODO: dynamic wait time? const?
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ctrl->enqueueAction([this, id, sequenceId]() {
      processChunked(id, sequenceId);
    });
  }).detach();
}
