#pragma once

#include "ChunkedSend.hpp"

struct ChunkedImage : ChunkedSend, public std::enable_shared_from_this<ChunkedImage> {
  ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height);

  static const int32_t DEPTH{4};
  int32_t width;
  int32_t height;

  ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) override;
private:
  bool compressData();
};
