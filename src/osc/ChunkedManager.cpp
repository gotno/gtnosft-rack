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
  chunked->init();
  chunkedSends.emplace(chunked->id, std::unique_ptr<ChunkedSend>(chunked));
  processChunked(chunked->id);
}

void ChunkedManager::ack(int32_t id, int32_t chunkNum) {
  if (chunkedExists(id)) getChunked(id)->ack(chunkNum);
}

bool ChunkedManager::isProcessing(int32_t id) {
  return chunkedExists(id) && !getChunked(id)->sendSucceeded();
}

ChunkedSend* ChunkedManager::findChunked(int32_t id) {
  if (!chunkedExists(id)) return NULL;
  return chunkedSends.at(id).get();
}

bool ChunkedManager::chunkedExists(int32_t id) {
  return chunkedSends.count(id) != 0;
}

ChunkedSend* ChunkedManager::getChunked(int32_t id) {
  assert(chunkedExists(id));
  return chunkedSends.at(id).get();
}

void ChunkedManager::processChunked(int32_t id) {
  if (!chunkedExists(id)) return;
  ChunkedSend* chunkedSend = getChunked(id);

  bool sendFailed = chunkedSend->sendFailed();
  // if (sendFailed) WARN("processing chunked send %d: send failed", id);

  bool sendSucceeded = chunkedSend->sendSucceeded();
  // if (sendSucceeded) INFO("processing chunked send %d: finished", id);

  if (sendFailed || sendSucceeded) {
    chunkedSends.erase(id);
    return;
  }

  std::vector<int32_t> unackedChunkNums;
  chunkedSend->getUnackedChunkNums(unackedChunkNums);

  for (int32_t chunkNum : unackedChunkNums) {
    ChunkedSendBundler* bundler =
      chunkedSend->getBundlerForChunk(chunkNum);

    bundler->noopCheck = [this, id, chunkNum](){
      if (!chunkedExists(id)) return true;
      if (getChunked(id)->sendFailed()) return true;
      if (getChunked(id)->acked(chunkNum)) return true;
      return false;
    };

    bundler->onBundleSent = [this, id, chunkNum](){
      if (!chunkedExists(id)) return;
      getChunked(id)->registerChunkSent(chunkNum);
    };

    osctx->enqueueBundler(bundler);
  }

  std::thread([this, id]() {
    // TODO: dynamic wait time? const?
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ctrl->enqueueAction([this, id]() {
      processChunked(id);
    });
  }).detach();
}
