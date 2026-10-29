#pragma once
#include "../plugin.hpp"
#include "AudioEncoder.hpp"
#include "Packet.hpp"
#include "Presets.hpp"
#include "UdpAudioSocket.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

struct bCast;
struct bCastConfig;

namespace bcast {

// Drains the module's ring, resamples to 48 kHz, frames, encodes and sends.
// Owned by the module; never touches the engine.
class AudioStreamer {
public:
  explicit AudioStreamer(bCast* module) : module(module) {}
  ~AudioStreamer() { stop(); }
  AudioStreamer(const AudioStreamer&) = delete;
  AudioStreamer& operator=(const AudioStreamer&) = delete;

  void start();
  void stop();

private:
  using clock = std::chrono::steady_clock;
  static constexpr int FAILING_AFTER = 50;

  bCast* module;
  std::atomic<bool> running{false};
  std::thread thread;

  // streamer-thread state
  Preset preset = presetById(DEFAULT_PRESET);
  std::string host;
  int port = 0;
  bool wantOpen = false;
  bool openWarned = false;
  bool highResTimer = false;
  clock::time_point lastOpenAttempt;
  int inRate = 0;
  uint32_t streamId = 0;
  uint16_t epoch = 0;
  uint32_t seq = 0;
  int sendFailures = 0;

  AudioEncoder encoder;
  UdpAudioSocket socket;
  rack::dsp::SampleRateConverter<2> src;
  std::vector<rack::dsp::Frame<2>> pending;    // engine rate, not yet resampled
  std::vector<rack::dsp::Frame<2>> resampled;  // scratch
  std::vector<float> accum;                    // 48 kHz interleaved, not yet framed
  std::deque<EncodedFrame> history;            // newest first, consecutive seqs
  std::vector<uint8_t> datagram;

  void run();
  void reset(const bCastConfig& config, int rate);
  void setStreaming(bool on);
  bool openSocket();
  void pump();
  void emitFrame(const float* pcm);
};

}
