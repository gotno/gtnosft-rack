#include "ImageChunkPacker.hpp"

ImageChunkPacker::ImageChunkPacker(ChunkedImage* _chunkedImage):
  chunkedImage(_chunkedImage) {}

ImageChunkPacker::~ImageChunkPacker() {
  chunkedImage = NULL;
}

void ImageChunkPacker::pack(osc::OutboundPacketStream& message) {
  const int32_t& chunkSize = ChunkedImage::CHUNK_SIZE;
  char chunk[chunkSize];
  memcpy(chunk, chunkedImage->pixels + chunkSize * chunkNum, chunkSize); 

	/* * int: image width */
	/* * int: image height */
	/* * int: total chunks */
	/* * int: start index for `memcpy` */
	/* * blob: the chunk */
	/* * int as uid */

  message << chunkedImage->id
		<< chunkedImage->width
		<< chunkedImage->height
    << chunkNum
		<< chunkedImage->numChunks
		<< chunkSize
		<< chunk
		;
}
