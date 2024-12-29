#include "ImageChunkPacker.hpp"

#include "../ChunkedSend/ChunkedImage.hpp"
#include "../../../dep/oscpack/osc/OscTypes.h"

ImageChunkPacker::ImageChunkPacker(int32_t _chunkNum, ChunkedImage* _chunkedImage):
  chunkNum(_chunkNum), chunkedImage(_chunkedImage) {
    path = "/chunked_image";
  }

ImageChunkPacker::~ImageChunkPacker() {
  chunkedImage = NULL;
}

void ImageChunkPacker::pack(osc::OutboundPacketStream& message) {
  const int32_t& chunkSize = chunkedImage->chunkSize;
  osc::Blob chunk(chunkedImage->data + chunkSize * chunkNum, chunkSize);

  message << chunkedImage->id
    << chunkedImage->width
    << chunkedImage->height
    << chunkedImage->numChunks
    << chunkSize
    << chunkNum
    << chunk
    ;

  chunkedImage->registerChunkSent(chunkNum);
}
