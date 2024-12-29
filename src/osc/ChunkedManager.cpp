#include "ChunkedManager.hpp"

#include "OscSender.hpp"
#include "ChunkedSend/ChunkedSend.hpp"
#include "MessagePacker/ImageChunkPacker.hpp"

ChunkedManager::ChunkedManager(OscSender* sender): osctx(sender) {}

ChunkedManager::~ChunkedManager() {
  for (auto& pair : chunkedSends)
    delete pair.second;
}


void ChunkedManager::add(ChunkedSend* chunked) {
  chunkedSends.emplace(chunked->id, chunked);
  processChunked(chunked->id);
}

ChunkedSend* ChunkedManager::getChunked(int32_t id) {
  assert(chunkedSends.count(id) != 0);
  return chunkedSends.at(id);
}

void ChunkedManager::processChunked(int32_t id) {
  std::vector<int32_t> unackedChunkNums;
  getChunked(id)->getUnackedChunkNums(unackedChunkNums);

  if (unackedChunkNums.size() == 0) {
    delete getChunked(id);
    chunkedSends.erase(id);
    return;
  }

  for (int32_t chunkNum : unackedChunkNums)
    osctx->enqueueMessage(getChunked(id)->getPackerForChunk(chunkNum));

  // enqueue reprocess
}

// void ChunkedManager::later();
