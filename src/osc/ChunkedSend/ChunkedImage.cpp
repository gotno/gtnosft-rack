#include "ChunkedImage.hpp"

#include "../ChunkedManager.hpp"
#include "../Bundler/ChunkedImageBundler.hpp"

#define QOI_IMPLEMENTATION
#include "qoi/qoi.h"

#include <algorithm>

ChunkedImage::ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height):
  ChunkedSend(_pixels, _width * _height * ChunkedImage::DEPTH),
  width(_width), height(_height) {
    // TODO?: throw on compression failure, catch in caller and dispose
    if (!compressData()) WARN("failed to compress image data");
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

ChunkedSendBundler* ChunkedImage::getBundlerForChunk(
  int32_t chunkNum,
  ChunkedManager* chunkman
) {
  return new ChunkedImageBundler(chunkNum, id, chunkman);
}
