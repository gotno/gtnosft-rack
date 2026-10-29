#pragma once
#include <cstdint>

// Mirrored by tools/bench/audio_analysis.py and RackSim's
// RSNetAudioProtocol.h. Only the id travels over the wire.
namespace bcast {

enum class Codec : uint8_t { Pcm = 0, Opus = 1 };

struct Preset {
  int id;
  const char* name;
  Codec codec;
  int frameSamples; // per channel, at 48 kHz
  int bitrate;      // Opus only
  int redundancy;   // previous frames repeated in each packet
  int targetMs;     // client jitter buffer target
  int minMs;
  int maxMs;
};

constexpr int SAMPLE_RATE = 48000;
constexpr int CHANNELS = 2;
constexpr int DEFAULT_PRESET = 1;

constexpr Preset PRESETS[] = {
  {0, "Lowest latency", Codec::Opus, 240, 192000, 0, 20, 10, 60},
  {1, "Balanced", Codec::Opus, 480, 160000, 1, 40, 20, 120},
  {2, "Robust", Codec::Opus, 960, 160000, 2, 80, 40, 200},
  {3, "Uncompressed", Codec::Pcm, 240, 0, 0, 20, 10, 60},
};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

constexpr bool isValidPreset(int id) { return id >= 0 && id < PRESET_COUNT; }
constexpr const Preset& presetById(int id) {
  return PRESETS[isValidPreset(id) ? id : DEFAULT_PRESET];
}

}
