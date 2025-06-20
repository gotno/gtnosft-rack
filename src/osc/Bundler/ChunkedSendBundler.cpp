#include "ChunkedSendBundler.hpp"

#include "../OscConstants.hpp"

#include "../ChunkedManager.hpp"
#include "../ChunkedSend/ChunkedSend.hpp"

ChunkedSendBundler::ChunkedSendBundler(
  std::string _address,
  int32_t _chunkedSendId,
  int32_t _chunkNum,
  int32_t _numChunks,
  int32_t _chunkSize,
  int64_t _totalSize,
  uint8_t* data,
  int32_t thisChunkSize
): Bundler("ChunkedSendBundler"),
  address(_address),
  chunkedSendId(_chunkedSendId),
  chunkNum(_chunkNum),
  numChunks(_numChunks),
  chunkSize(_chunkSize),
  totalSize(_totalSize) {
    messages.emplace_back(
      address,
      [this, data, thisChunkSize](osc::OutboundPacketStream& pstream) {
        bundleMetadata(pstream);

        const int32_t offset = chunkSize * chunkNum;
        pstream << osc::Blob(data + offset, thisChunkSize);
      }
    );
  }

size_t ChunkedSendBundler::getAvailableBundleSpace() {
  char* msgBuffer = new char[MSG_BUFFER_SIZE];
  osc::OutboundPacketStream pstream(msgBuffer, MSG_BUFFER_SIZE);

  pstream << osc::BeginBundleImmediate
    << osc::BeginMessage(address.c_str());
  bundleMetadata(pstream);
  pstream << osc::EndMessage
    << osc::EndBundle;

  // 7 bytes to cover the blob type tag and rounding up to a multiple of 4 bytes
  size_t availableSpace = pstream.Capacity() - (pstream.Size() + 7);

  delete[] msgBuffer;

  return availableSpace;
}

void ChunkedSendBundler::bundleMetadata(osc::OutboundPacketStream& pstream) {
  pstream << chunkedSendId
    << chunkNum
    << numChunks
    << chunkSize
    << totalSize
    ;
}
