#pragma once

#include "ChunkedSend.hpp"

#include "../../renderer/Renderer.hpp"

struct ChunkedImage : ChunkedSend {
  ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height);
  ChunkedImage(const RenderResult& result);

  static const int32_t DEPTH{4};
  int32_t width;
  int32_t height;

  ChunkedSendBundler* getBundlerForChunk(
    int32_t chunkNum,
    ChunkedManager* chunkman
  ) override;
private:
  bool compressData();
};
