#pragma once

#include "Bundler.hpp"

// /set/audio_stream: the bCast expander's stream, for the client to play
struct AudioStreamBundler : Bundler {
  AudioStreamBundler(
    int64_t moduleId,
    uint32_t streamId,
    int32_t presetId,
    bool enabled,
    uint16_t epoch
  );
};

// /set/audio/enabled/ack and /set/audio/preset/ack
struct AudioAckBundler : Bundler {
  AudioAckBundler(const std::string& address, int64_t moduleId, int32_t value, bool success);
};
