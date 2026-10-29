#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

// Paces texture chunk sends while an audio stream plays, so overlay bursts
// don't fill the Wi-Fi queues ahead of audio. A token bucket whose rate is
// steered by the client's audio stats (AIMD). Thread-safe.
class ChunkPacer {
public:
  using clock = std::chrono::steady_clock;

  struct AudioStats {
    int underruns = 0;
    int lost = 0;
    int recovered = 0;
    float jitterMs = 0.f;
    float bufferMs = 0.f;
  };

  static constexpr double BURST_BYTES = 8 * 1452;
  static constexpr double MAX_MBPS = 400;
  static constexpr double MIN_MBPS = 20;
  static constexpr double INCREASE_MBPS = 10;
  static constexpr double DECREASE = 0.7;
  static constexpr clock::duration STATS_TIMEOUT = std::chrono::seconds(2);
  static constexpr clock::duration JITTER_WINDOW = std::chrono::seconds(10);

  // bench A/B toggle
  std::atomic<bool> enabled{true};

  void setActive(bool active, clock::time_point now);
  void onStats(const AudioStats& stats, clock::time_point now);
  clock::time_point nextSendTime(clock::time_point now);
  void consume(size_t bytes, clock::time_point now);
  double rateMbps(clock::time_point now);

private:
  std::mutex mutex;
  bool active = false;
  double rate = 0;  // Mbit/s, 0 = unlimited
  double tokens = BURST_BYTES;
  clock::time_point lastRefill{};
  clock::time_point lastStats{};
  bool haveBaseline = false;
  AudioStats baseline;
  std::deque<std::pair<clock::time_point, float>> jitter;

  void expireIfStale(clock::time_point now);
  void refill(clock::time_point now);
  bool jitterTrouble(float jitterMs, clock::time_point now);
};
