#include "Packet.hpp"

#include <cstring>

namespace bcast {

namespace {
void put16(uint8_t* p, uint16_t v) {
  p[0] = v & 0xff;
  p[1] = v >> 8;
}
void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xff;
}
}

size_t writePacket(
  uint8_t* out,
  size_t cap,
  const PacketHeader& header,
  const std::vector<const EncodedFrame*>& frames
) {
  if (frames.empty() || frames.size() > 255) return 0;
  size_t size = HEADER_SIZE;
  for (const EncodedFrame* f : frames) {
    if (f->size() > 0xffff) return 0;
    size += 2 + f->size();
  }
  if (size > cap) return 0;

  out[0] = MAGIC[0];
  out[1] = MAGIC[1];
  out[2] = PROTOCOL_VERSION;
  out[3] = 0;
  put32(out + 4, header.streamId);
  put16(out + 8, header.epoch);
  out[10] = (uint8_t)header.codec;
  out[11] = CHANNELS;
  put16(out + 12, header.frameSamples);
  out[14] = (uint8_t)frames.size();
  out[15] = header.presetId;
  put32(out + 16, header.seq);

  uint8_t* p = out + HEADER_SIZE;
  for (const EncodedFrame* f : frames) {
    put16(p, (uint16_t)f->size());
    p += 2;
    std::memcpy(p, f->data(), f->size());
    p += f->size();
  }
  return size;
}

}
