#include "ChunkedSendBundler.hpp"
#include "../ChunkedSend/ChunkedSend.hpp"

ChunkedSendBundler::ChunkedSendBundler(
  int32_t _chunkNum,
  ChunkedSend* _chunkedSend
): Bundler("ChunkedSendBundler"), chunkNum(_chunkNum), chunkedSend(_chunkedSend) {
  messages.emplace_back(
    "/chunked",
    [this](osc::OutboundPacketStream& pstream) {
      bundleMetadata(pstream);
      pstream << chunkedSend->getBlobForChunk(chunkNum);
    }
  );
}

size_t ChunkedSendBundler::getChunkSize(osc::OutboundPacketStream& pstream) {
  bundleMetadata(pstream);
  return pstream.AvailableBundleSpace();
}

bool ChunkedSendBundler::isNoop() {
  // send failed or this chunk already ack'd
  return chunkedSend->sendFailed() || chunkedSend->chunkAckTimes.count(chunkNum);
}

void ChunkedSendBundler::finish() {
  chunkedSend->registerChunkSent(chunkNum);
}

void ChunkedSendBundler::bundleMetadata(osc::OutboundPacketStream& pstream) {
  pstream << chunkedSend->id
    << chunkNum
    << chunkedSend->numChunks
    << chunkedSend->chunkSize
    << chunkedSend->size
    ;
}
