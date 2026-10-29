#include "test.hpp"
#include "audio/AudioEncoder.hpp"
#include "audio/Packet.hpp"

#include <cmath>
#include <opus/opus.h>

using namespace bcast;

static std::vector<float> sine(int frames, int offset, float amp = 0.5f) {
  std::vector<float> out(frames * 2);
  for (int i = 0; i < frames; ++i) {
    float v = amp * std::sin(2.f * (float)M_PI * 1000.f * (i + offset) / SAMPLE_RATE);
    out[2 * i] = v;
    out[2 * i + 1] = v;
  }
  return out;
}

TEST(encoder_pcm_s16le) {
  AudioEncoder enc;
  CHECK(enc.reset(PRESETS[3]));
  std::vector<float> in(240 * 2, 0.f);
  in[0] = 1.f;
  in[1] = -1.f;
  in[2] = 1.5f;  // clamps
  in[3] = 0.5f;
  uint8_t out[2000];
  int n = enc.encode(in.data(), out, sizeof(out));
  CHECK_EQ(n, 240 * 4);
  CHECK_EQ(out[0], 0xFF); CHECK_EQ(out[1], 0x7F);  // 32767
  CHECK_EQ(out[2], 0x01); CHECK_EQ(out[3], 0x80);  // -32767
  CHECK_EQ(out[4], 0xFF); CHECK_EQ(out[5], 0x7F);
  CHECK_EQ(out[6], 0x00); CHECK_EQ(out[7], 0x40);  // lrint(16383.5) = 16384
}

TEST(encoder_pcm_rejects_small_buffer) {
  AudioEncoder enc;
  enc.reset(PRESETS[3]);
  std::vector<float> in(240 * 2, 0.f);
  uint8_t out[100];
  CHECK_EQ(enc.encode(in.data(), out, sizeof(out)), -1);
}

TEST(encoder_opus_fits_packet_and_round_trips) {
  for (int id = 0; id <= 2; ++id) {
    const Preset& p = PRESETS[id];
    AudioEncoder enc;
    CHECK(enc.reset(p));
    int err = 0;
    OpusDecoder* dec = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    CHECK_EQ(err, OPUS_OK);

    int cap = (int)maxFrameBytes(1 + p.redundancy);
    std::vector<uint8_t> out(cap);
    std::vector<float> decoded(p.frameSamples * 2);
    double rms = 0;
    for (int f = 0; f < 50; ++f) {
      std::vector<float> in = sine(p.frameSamples, f * p.frameSamples);
      int n = enc.encode(in.data(), out.data(), cap);
      CHECK(n > 0);
      CHECK(n <= cap);
      int got = opus_decode_float(dec, out.data(), n, decoded.data(), p.frameSamples, 0);
      CHECK_EQ(got, p.frameSamples);
      if (f == 49) {
        for (float v : decoded) rms += v * v;
        rms = std::sqrt(rms / decoded.size());
      }
    }
    // 0.5 amplitude sine: rms 0.354
    CHECK(std::fabs(rms - 0.354) < 0.05);
    opus_decoder_destroy(dec);
  }
}
