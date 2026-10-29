#pragma once
#include <cstdint>

#include "Presets.hpp"

struct OpusEncoder;

namespace bcast {

// Encodes one frame of 48 kHz interleaved stereo as Opus or s16le PCM
struct AudioEncoder {
  AudioEncoder() = default;
  ~AudioEncoder();
  AudioEncoder(const AudioEncoder&) = delete;
  AudioEncoder& operator=(const AudioEncoder&) = delete;

  // false if the Opus encoder couldn't be created
  bool reset(const Preset& preset);
  // in: preset.frameSamples * 2 floats. returns bytes written, or -1
  int encode(const float* in, uint8_t* out, int cap);

private:
  OpusEncoder* opus = nullptr;
  Preset preset = PRESETS[DEFAULT_PRESET];
  void destroy();
};

}
