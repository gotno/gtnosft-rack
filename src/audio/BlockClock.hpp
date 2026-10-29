#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

namespace bcast {

// Paces engine blocks against absolute deadlines, so a late wakeup is made up
// on the next block instead of slowing the engine (Rack's fallback clock
// sleeps relative to "now" and drifts slow, which the client hears as
// underruns).
struct BlockClock {
  using clock = std::chrono::steady_clock;

  // ~5 ms blocks
  static int blockFrames(double sampleRate) {
    return std::max(16, (int)std::lround(sampleRate / 200.0));
  }

  // call after stepping `frames`; returns when the next block is due
  clock::time_point next(int frames, double sampleRate, clock::time_point now) {
    if (!running || sampleRate != rate) {
      base = now;
      total = 0;
      rate = sampleRate;
      running = true;
    }
    total += frames;
    clock::time_point deadline = base + std::chrono::duration_cast<clock::duration>(
      std::chrono::duration<double>(total / rate));
    if (now - deadline > MAX_LAG) {
      base = now;
      total = frames;
      deadline = base + std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(total / rate));
    }
    return deadline;
  }

  void reset() { running = false; }

private:
  static constexpr clock::duration MAX_LAG = std::chrono::milliseconds(100);
  clock::time_point base;
  double total = 0;
  double rate = 0;
  bool running = false;
};

}
