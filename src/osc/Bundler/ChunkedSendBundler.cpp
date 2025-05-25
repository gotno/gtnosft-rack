#include "ChunkedSendBundler.hpp"

#include "../ChunkedManager.hpp"
#include "../ChunkedSend/ChunkedSend.hpp"

ChunkedSendBundler::ChunkedSendBundler(
  int32_t _chunkNum,
  int32_t _chunkedSendId,
  ChunkedManager* _chunkedManager
): Bundler("ChunkedSendBundler"),
  chunkNum(_chunkNum),
  chunkedSendId(_chunkedSendId),
  chunkman(_chunkedManager) {
    messages.emplace_back(
      "/chunked",
      [this](osc::OutboundPacketStream& pstream) {
        ChunkedSend* chunkedSend = chunkman->findChunked(chunkedSendId);
        bundleMetadata(pstream, chunkedSend);
        pstream << chunkedSend->getBlobForChunk(chunkNum);
      }
    );
  }

size_t ChunkedSendBundler::getChunkSize(osc::OutboundPacketStream& pstream) {
  pstream << osc::BeginBundleImmediate
    << osc::BeginMessage(messages[0].first.c_str());
  bundleMetadata(pstream, chunkman->findChunked(chunkedSendId));
  pstream << osc::EndMessage
    << osc::EndBundle;

  // 7 bytes to cover the blob type tag and rounding up to a multiple of 4 bytes
  return pstream.Capacity() - (pstream.Size() + 7);
}

bool ChunkedSendBundler::isNoop() {
  ChunkedSend* chunkedSend = chunkman->findChunked(chunkedSendId);
  if (!chunkedSend) return true;
  // send failed or this chunk already ack'd
  return chunkedSend->sendFailed() || chunkedSend->chunkAckTimes.count(chunkNum);
}

void ChunkedSendBundler::finish() {
  chunkman->findChunked(chunkedSendId)->registerChunkSent(chunkNum);
}

void ChunkedSendBundler::bundleMetadata(
  osc::OutboundPacketStream& pstream,
  ChunkedSend* chunkedSend
) {
  pstream << chunkedSend->id
    << chunkNum
    << chunkedSend->numChunks
    << chunkedSend->chunkSize
    << chunkedSend->size
    ;
}
