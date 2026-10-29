#include "AudioBundlers.hpp"

AudioStreamBundler::AudioStreamBundler(
  int64_t moduleId,
  uint32_t streamId,
  int32_t presetId,
  bool enabled,
  uint16_t epoch
): Bundler("AudioStreamBundler") {
  messages.emplace_back(
    "/set/audio_stream",
    [=](osc::OutboundPacketStream& pstream) {
      // OSC has no unsigned int; the client reads streamId back as uint32
      pstream << moduleId
        << (int32_t)streamId
        << presetId
        << (int32_t)enabled
        << (int32_t)epoch;
    }
  );
}

AudioAckBundler::AudioAckBundler(
  const std::string& address,
  int64_t moduleId,
  int32_t value,
  bool success
): Bundler("AudioAckBundler") {
  messages.emplace_back(
    address,
    [=](osc::OutboundPacketStream& pstream) {
      pstream << moduleId << value << (int32_t)success;
    }
  );
}
