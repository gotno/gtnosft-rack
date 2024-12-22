#include "MessagePackager.hpp"
#include "ChunkedImage.hpp"

struct ImageChunkPackager : MessagePacker {
  ImageChunkPackager(ChunkedImage* _chunkedImage);
  ~ImageChunkPackager();

  ChunkedImage* chunkedImage{NULL};
  int chunkNum{0};

  void process(osc::OutboundPacketStream& message) override;
};
