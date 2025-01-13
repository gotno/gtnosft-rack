#include "ChunkedImage.hpp"

#include "../MessagePacker/ImageChunkPacker.hpp"

ChunkedImage::ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height):
  ChunkedSend(_pixels, _width * _height * ChunkedImage::DEPTH),
  width(_width), height(_height) {
    chunkSize = MAX_CHUNK_SIZE - 1024;
    // chunkSize = 1024 * 32;
    init();
  }

MessagePacker* ChunkedImage::getPackerForChunk(int32_t chunkNum) {
  return new ImageChunkPacker(chunkNum, this);
}
