#include "ChunkedSendBundler.hpp"
#include "../ChunkedSend/ChunkedSend.hpp"

ChunkedSendBundler::ChunkedSendBundler(
  int32_t _chunkNum,
  ChunkedSend* _chunkedSend
): Bundler("ChunkedSendBundler"),
  chunkNum(_chunkNum), chunkedSend(_chunkedSend) {
  messages.emplace_back(
    "/chunked",
    [this](osc::OutboundPacketStream& pstream) {
      bundleMetadata(pstream);

      // if this is the first chunk bundled,
      // report the available space and start over
      if (!chunkedSend->ready) {
        chunkedSend->setChunkSize(pstream.AvailableBundleSpace());
        resetBundle(pstream);
        messages[0].second(pstream);
        return;
      }

      pstream << chunkedSend->getBlobForChunk(chunkNum);
    }
  );
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

void ChunkedSendBundler::resetBundle(osc::OutboundPacketStream& pstream) {
  pstream.Clear();
  pstream << osc::BeginBundleImmediate;
}
