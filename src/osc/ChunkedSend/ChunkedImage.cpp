#include "ChunkedImage.hpp"

#include "../MessagePacker/ImageChunkPacker.hpp"

#define QOI_IMPLEMENTATION
#include "../../../dep/qoi/qoi.h"

#include <algorithm>

ChunkedImage::ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height):
  ChunkedSend(_pixels, _width * _height * ChunkedImage::DEPTH),
  width(_width), height(_height) {
    // last checked, metadata takes 84 bytes, but let's allow for a little more.
    int32_t largestPossibleChunk = MAX_CHUNK_SIZE - 128;
    int32_t preferredChunkSize = 1024 * 48;
    chunkSize = std::min(preferredChunkSize, largestPossibleChunk);
    calculateNumChunks();
  }

void ChunkedImage::compress() {
  if ((isCompressed = compressData())) {
    calculateNumChunks();
    return;
  }

  WARN("unable to compress image!");
}

bool ChunkedImage::compressData() {
  qoi_desc desc;
  desc.width = width;
  desc.height = height;
  desc.channels = ChunkedImage::DEPTH;
  desc.colorspace = 0;

  int compressedLength{0};
  void* compressedData = qoi_encode(data, &desc, &compressedLength);

  if (!compressedData) return false;

  // INFO(
  //   "image %d bytes, %d compressed",
  //   width * height * ChunkedImage::DEPTH,
  //   compressedLength
  // );

  delete[] data;
  data = new uint8_t[compressedLength];
  size = compressedLength;
  memcpy(data, compressedData, size);
  free(compressedData);

  return true;
}

MessagePacker* ChunkedImage::getPackerForChunk(int32_t chunkNum) {
  return new ImageChunkPacker(chunkNum, this);
}
