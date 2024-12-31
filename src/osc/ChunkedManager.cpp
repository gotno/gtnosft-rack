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

bool ChunkedManager::chunkedExists(int32_t id) {
  return chunkedSends.count(id) != 0;
}

ChunkedSend* ChunkedManager::getChunked(int32_t id) {
  assert(chunkedExists(id));
  return chunkedSends.at(id);
}

void ChunkedManager::processChunked(int32_t id) {
  std::vector<int32_t> unackedChunkNums;
  getChunked(id)->getUnackedChunkNums(unackedChunkNums);
  bool sendFailed = getChunked(id)->sendFailed();

  if (sendFailed || unackedChunkNums.size() == 0) {
    if (sendFailed) {
      WARN("processing chunked send %d: send failed", id);
    } else {
      INFO("processing chunked send %d: finished", id);
    }

    delete getChunked(id);
    chunkedSends.erase(id);
    return;
  }

  INFO(
    "processing chunked send %d: %lld unacked chunks",
    id,
    unackedChunkNums.size()
  );

  for (int32_t chunkNum : unackedChunkNums)
    osctx->enqueueMessage(getChunked(id)->getPackerForChunk(chunkNum));

  std::thread([&, id]() { reprocessChunked(id); }).detach();
}

void ChunkedManager::reprocessChunked(int32_t id) {
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  processChunked(id);
}
