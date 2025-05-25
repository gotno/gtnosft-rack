#include "ChunkedManager.hpp"

#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
// #include "Bundler/Bundler.hpp"
#include "Bundler/ChunkedSendBundler.hpp"

#include <thread>
#include <chrono>

ChunkedManager::ChunkedManager(OscSender* sender): osctx(sender) {}
ChunkedManager::~ChunkedManager() {}

void ChunkedManager::add(std::shared_ptr<ChunkedSend> chunked) {
  if (chunked->numChunks == 0) {
    WARN("chunkman skipping chunked send with no chunks.");
    return;
  }
  chunkedSends.emplace(chunked->id, std::move(chunked));
  processChunked(chunked->id);
}

void ChunkedManager::ack(int32_t id, int32_t chunkNum) {
  if (chunkedExists(id)) getChunked(id)->ack(chunkNum);
}

bool ChunkedManager::isProcessing(int32_t id) {
  return chunkedExists(id) && !getChunked(id)->sendSucceeded();
}

bool ChunkedManager::chunkedExists(int32_t id) {
  return chunkedSends.count(id) != 0;
}

std::shared_ptr<ChunkedSend> ChunkedManager::getChunked(int32_t id) {
  assert(chunkedExists(id));
  return chunkedSends.at(id);
}

void ChunkedManager::processChunked(int32_t id) {
  std::shared_ptr<ChunkedSend> chunkedSend = getChunked(id);

  bool sendFailed = chunkedSend->sendFailed();
  if (sendFailed) WARN("processing chunked send %d: send failed", id);

  bool sendSucceeded = chunkedSend->sendSucceeded();
  // if (sendSucceeded)
  //   INFO("processing chunked send %d: finished", id);

  if (sendFailed || sendSucceeded) {
    chunkedSends.erase(id);
    return;
  }

  std::vector<int32_t> unackedChunkNums;
  chunkedSend->getUnackedChunkNums(unackedChunkNums);

  for (int32_t chunkNum : unackedChunkNums)
    osctx->enqueueBundler(chunkedSend->getBundlerForChunk(chunkNum));

  std::thread([this, id]() { reprocessChunked(id); }).detach();
}

void ChunkedManager::reprocessChunked(int32_t id) {
  // TODO: dynamic wait time? const?
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  if (chunkedExists(id)) processChunked(id);
}
