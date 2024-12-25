#include "MessagePacker.hpp"
#include "ChunkedImage.hpp"

struct ImageChunkPacker : MessagePacker {
  ImageChunkPacker(ChunkedImage* _chunkedImage);
  ~ImageChunkPacker();

  ChunkedImage* chunkedImage{NULL};
  int chunkNum{0};

  void pack(osc::OutboundPacketStream& message) override;
};
