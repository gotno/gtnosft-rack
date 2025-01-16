#include "ChunkedImage.hpp"

#include "../MessagePacker/ImageChunkPacker.hpp"

#define QOI_IMPLEMENTATION
#include "../../dep/qoi/qoi.h"

ChunkedImage::ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height):
  ChunkedSend(_pixels, _width * _height * ChunkedImage::DEPTH),
  width(_width), height(_height) {
    chunkSize = MAX_CHUNK_SIZE - 1024;
    // chunkSize = 1024 * 32;

    isCompressed = compressData();
    if (!isCompressed) WARN("unable to compress image data!");

    init();
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

  INFO("image %d bytes, %d compressed", width * height * ChunkedImage::DEPTH, compressedLength);

  delete[] data;
  data = new uint8_t[compressedLength];
  size = compressedLength;
  memcpy(data, compressedData, compressedLength);
  free(compressedData);

  return true;
}

MessagePacker* ChunkedImage::getPackerForChunk(int32_t chunkNum) {
  return new ImageChunkPacker(chunkNum, this);
}
