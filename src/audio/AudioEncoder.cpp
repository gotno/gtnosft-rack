#include "AudioEncoder.hpp"

#include <algorithm>
#include <cmath>
#include <opus/opus.h>

namespace bcast {

AudioEncoder::~AudioEncoder() { destroy(); }

void AudioEncoder::destroy() {
  if (opus) opus_encoder_destroy(opus);
  opus = nullptr;
}

bool AudioEncoder::reset(const Preset& p) {
  destroy();
  preset = p;
  if (p.codec == Codec::Pcm) return true;

  int err = 0;
  opus = opus_encoder_create(
    SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err
  );
  if (err != OPUS_OK || !opus) {
    opus = nullptr;
    return false;
  }
  opus_encoder_ctl(opus, OPUS_SET_BITRATE(p.bitrate));
  opus_encoder_ctl(opus, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
  opus_encoder_ctl(opus, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
  opus_encoder_ctl(opus, OPUS_SET_COMPLEXITY(8));
  opus_encoder_ctl(opus, OPUS_SET_VBR(1));
  opus_encoder_ctl(opus, OPUS_SET_VBR_CONSTRAINT(1));
  // in-band FEC is SILK-only; redundancy is packet-level instead
  opus_encoder_ctl(opus, OPUS_SET_INBAND_FEC(0));
  return true;
}

int AudioEncoder::encode(const float* in, uint8_t* out, int cap) {
  int samples = preset.frameSamples * CHANNELS;
  if (preset.codec == Codec::Pcm) {
    if (cap < samples * 2) return -1;
    for (int i = 0; i < samples; ++i) {
      float v = std::clamp(in[i], -1.f, 1.f);
      int16_t s = (int16_t)std::lrint(v * 32767.f);
      out[2 * i] = (uint8_t)(s & 0xff);
      out[2 * i + 1] = (uint8_t)((uint16_t)s >> 8);
    }
    return samples * 2;
  }
  if (!opus) return -1;
  int n = opus_encode_float(opus, in, preset.frameSamples, out, cap);
  return n < 0 ? -1 : n;
}

}
