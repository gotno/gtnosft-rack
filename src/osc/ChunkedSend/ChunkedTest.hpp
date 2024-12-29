#pragma once

#include "ChunkedSend.hpp"

struct ChunkedTest : ChunkedSend {
  ChunkedTest(uint8_t* _data, size_t _size);
  MessagePacker* getPackerForChunk(int32_t chunkNum) override;
};
