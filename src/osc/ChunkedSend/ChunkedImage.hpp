#pragma once

#include "ChunkedSend.hpp"

// Forward declaration, if ChunkedImageBundler needs ChunkedImage.
// class ChunkedImageBundler; // Not strictly needed here as it's only a return type

struct ChunkedImage : ChunkedSend { // Removed std::enable_shared_from_this
  ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height);

  static const int32_t DEPTH{4};
  int32_t width;
  int32_t height;

  ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) override;
private:
  bool compressData();
};
