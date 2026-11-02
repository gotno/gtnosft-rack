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

struct Tx;
struct TxConfig;

namespace tx_af {

// drains the module's ring, resamples to 48 kHz, frames, encodes and sends.
// owned by the module; never touches the engine.
class AudioStreamer {
public:
  explicit AudioStreamer(Tx* module) : module(module) {}
  ~AudioStreamer() { stop(); }
  AudioStreamer(const AudioStreamer&) = delete;
  AudioStreamer& operator=(const AudioStreamer&) = delete;

  void start();
  void stop();

private:
  using clock = std::chrono::steady_clock;
  static constexpr int FAILING_AFTER = 50;

  Tx* module;
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

  // engine rate, not yet resampled
  std::vector<rack::dsp::Frame<2>> pending;
  // scratch
  std::vector<rack::dsp::Frame<2>> resampled;
  // 48 kHz interleaved, not yet framed
  std::vector<float> accum;
  // newest first, consecutive seqs
  std::deque<EncodedFrame> history;
  std::vector<uint8_t> datagram;

  void run();
  void reset(const TxConfig& config, int rate);
  void setStreaming(bool on);
  bool openSocket();
  void pump();
  void emitFrame(const float* pcm);
};

} // namespace tx_af
