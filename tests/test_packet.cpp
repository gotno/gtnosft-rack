#include "test.hpp"
#include "audio/Packet.hpp"

using namespace bcast;

TEST(packet_header_layout) {
  EncodedFrame newest{0xAA, 0xBB, 0xCC};
  EncodedFrame older{0x11};
  PacketHeader h{0x04030201u, 0x0605, Codec::Opus, 480, 1, 0x0A090807u};
  uint8_t buf[MAX_DATAGRAM];

  size_t n = writePacket(buf, sizeof(buf), h, {&newest, &older});

  CHECK_EQ(n, HEADER_SIZE + 2 + 3 + 2 + 1);
  const uint8_t expected[20] = {
    'R', 'A', 1, 0,
    0x01, 0x02, 0x03, 0x04,  // streamId
    0x05, 0x06,              // epoch
    1, 2,                    // codec, channels
    0xE0, 0x01,              // frameSamples 480
    2, 1,                    // frameCount, presetId
    0x07, 0x08, 0x09, 0x0A,  // seq
  };
  for (int i = 0; i < 20; ++i) CHECK_EQ(buf[i], expected[i]);
  CHECK_EQ(buf[20], 3);
  CHECK_EQ(buf[21], 0);
  CHECK_EQ(buf[22], 0xAA);
  CHECK_EQ(buf[24], 0xCC);
  CHECK_EQ(buf[25], 1);
  CHECK_EQ(buf[26], 0);
  CHECK_EQ(buf[27], 0x11);
}

TEST(packet_rejects_oversize) {
  EncodedFrame big(MAX_DATAGRAM, 0);
  PacketHeader h{1, 0, Codec::Pcm, 240, 3, 0};
  uint8_t buf[MAX_DATAGRAM];
  CHECK_EQ(writePacket(buf, sizeof(buf), h, {&big}), (size_t)0);
  CHECK_EQ(writePacket(buf, sizeof(buf), h, {}), (size_t)0);
}

TEST(packet_max_frame_bytes_fill_datagram) {
  for (int count = 1; count <= 3; ++count) {
    std::vector<EncodedFrame> frames(count, EncodedFrame(maxFrameBytes(count), 7));
    std::vector<const EncodedFrame*> ptrs;
    for (auto& f : frames) ptrs.push_back(&f);
    PacketHeader h{1, 0, Codec::Opus, 960, 2, 5};
    uint8_t buf[MAX_DATAGRAM];
    size_t n = writePacket(buf, sizeof(buf), h, ptrs);
    CHECK(n > MAX_DATAGRAM - 3);
    CHECK(n <= MAX_DATAGRAM);
  }
  CHECK_EQ(maxFrameBytes(3), (size_t)402);
}

TEST(presets_fallback_to_default) {
  CHECK_EQ(presetById(-1).id, DEFAULT_PRESET);
  CHECK_EQ(presetById(PRESET_COUNT).id, DEFAULT_PRESET);
  CHECK_EQ(presetById(3).id, 3);
  // PCM 5 ms stereo fits a single datagram
  CHECK(presetById(3).frameSamples * 4 <= (int)maxFrameBytes(1));
}
