#include "ChunkedManager.hpp"

#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
#include "MessagePacker/ImageChunkPacker.hpp"

#include <thread>
#include <chrono>

ChunkedManager::ChunkedManager(OscSender* sender): osctx(sender) {}

ChunkedManager::~ChunkedManager() {
  for (auto& pair : chunkedSends)
    delete pair.second;
}

void ChunkedManager::add(ChunkedSend* chunked) {
  chunkedSends.emplace(chunked->id, chunked);
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

ChunkedSend* ChunkedManager::getChunked(int32_t id) {
  assert(chunkedExists(id));
  return chunkedSends.at(id);
}

void ChunkedManager::processChunked(int32_t id) {
  ChunkedSend* chunkedSend = getChunked(id);

  bool sendFailed = chunkedSend->sendFailed();
  if (sendFailed) WARN("processing chunked send %d: send failed", id);

  std::vector<int32_t> unackedChunkNums;
  chunkedSend->getUnackedChunkNums(unackedChunkNums);
  // if (unackedChunkNums.size() == 0)
    // INFO("processing chunked send %d: finished", id);

  if (sendFailed || unackedChunkNums.size() == 0) {
    delete chunkedSend;
    chunkedSends.erase(id);
    return;
  }

  for (int32_t chunkNum : unackedChunkNums)
    osctx->enqueueMessage(chunkedSend->getPackerForChunk(chunkNum));

  std::thread([&, id]() { reprocessChunked(id); }).detach();
}

void ChunkedManager::reprocessChunked(int32_t id) {
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  if (chunkedExists(id)) processChunked(id);
}
