#include "ChunkedSendBundler.hpp"
// #include "../ChunkedSend/ChunkedSend.hpp" // No longer directly needed

ChunkedSendBundler::ChunkedSendBundler(
  int32_t _chunkNum,
  int32_t id,
  int32_t numChunks,
  int32_t chunkSize,
  int64_t totalSize,
  std::shared_ptr<uint8_t[]> data_ptr,
  bool isNoopStatus,
  std::function<void(int32_t)> regChunkSentCb
) : Bundler("ChunkedSendBundler"),
    chunkNum(_chunkNum),
    sendIdM(id),
    numChunksM(numChunks),
    chunkSizeM(chunkSize),
    totalSizeM(totalSize),
    pixelDataM(data_ptr),
    isNoopM(isNoopStatus),
    registerChunkSentCbM(regChunkSentCb) {
  messages.emplace_back(
    "/chunked",
    [this](osc::OutboundPacketStream& pstream) {
      bundleMetadata(pstream); // Uses member variables now
      // Replicate ChunkedSend::getBlobForChunk logic using member variables
      int32_t currentChunkSize = (this->chunkNum == this->numChunksM - 1)
                               ? this->totalSizeM - (this->numChunksM - 1) * this->chunkSizeM
                               : this->chunkSizeM;
      // Ensure chunkSizeM is not zero if used in offset calculation directly and numChunksM > 1
      // However, if numChunksM is 1, offset calculation is fine.
      // If numChunksM > 1, chunkSizeM must have been non-zero to calculate numChunksM.
      const int32_t offset = this->chunkSizeM * this->chunkNum;
      pstream << osc::Blob(this->pixelDataM.get() + offset, currentChunkSize);
    }
  );
}

size_t ChunkedSendBundler::getChunkSize(osc::OutboundPacketStream& pstream) {
  bundleMetadata(pstream); // Relies on updated bundleMetadata
  return pstream.AvailableBundleSpace();
}

bool ChunkedSendBundler::isNoop() {
  return isNoopM;
}

void ChunkedSendBundler::finish() {
  if(registerChunkSentCbM) {
    registerChunkSentCbM(chunkNum);
  }
}

void ChunkedSendBundler::bundleMetadata(osc::OutboundPacketStream& pstream) {
  pstream << sendIdM
    << chunkNum // chunkNum is a direct member
    << numChunksM
    << chunkSizeM
    << totalSizeM;
}
