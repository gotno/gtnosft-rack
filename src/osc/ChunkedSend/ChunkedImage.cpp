#include "ChunkedImage.hpp"

#include "../ChunkedManager.hpp"
#include "../Bundler/ChunkedImageBundler.hpp"

#define QOI_IMPLEMENTATION
#include "qoi/qoi.h"

#include <cstring>
#include <vector>

ChunkedImage::ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height):
  ChunkedSend(_pixels, _width * _height * ChunkedImage::DEPTH),
  width(_width), height(_height) {}

ChunkedImage::ChunkedImage(const RenderResult& result):
  ChunkedImage(result.pixels, result.width, result.height) {}

void ChunkedImage::init() {
  flipRows();
  BENCH(if (trace) trace->stamp(bench::Stage::Flipped);)

  // TODO?: throw on compression failure, catch in caller and dispose
  bool compressionFailure = !compressData();
  if (compressionFailure) WARN("failed to compress image data");

  ChunkedSend::init();
}

// rendered pixels arrive bottom-up from GL; clients expect top-down
void ChunkedImage::flipRows() {
  const size_t rowBytes = (size_t)width * DEPTH;
  std::vector<uint8_t> tmp(rowBytes);
  for (int32_t y = 0; y < height / 2; y++) {
    uint8_t* top = data + (size_t)y * rowBytes;
    uint8_t* bottom = data + (size_t)(height - y - 1) * rowBytes;
    std::memcpy(tmp.data(), top, rowBytes);
    std::memcpy(top, bottom, rowBytes);
    std::memcpy(bottom, tmp.data(), rowBytes);
  }
}

bool ChunkedImage::compressData() {
  BENCH(if (trace) trace->rawBytes = size;)

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

  BENCH(
    if (trace) {
      trace->stamp(bench::Stage::Compressed);
      trace->compressedBytes = size;
    }
  )

  return true;
}

ChunkedSendBundler* ChunkedImage::getBundlerForChunk(int32_t chunkNum) {
  int32_t thisChunkSize =
    chunkNum == numChunks - 1
      ? size - (numChunks - 1) * chunkSize
      : chunkSize;

  return new ChunkedImageBundler(
    id,
    sequenceId,
    chunkNum,
    numChunks,
    chunkSize,
    size,
    data,
    thisChunkSize,
    width,
    height
  );
}
