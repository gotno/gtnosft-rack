#include "test.hpp"
#include "audio/BlockClock.hpp"

#include <cmath>

using bcast::BlockClock;
using clk = BlockClock::clock;
using std::chrono::duration;
using std::chrono::microseconds;
using std::chrono::milliseconds;

static double seconds(clk::duration d) { return duration<double>(d).count(); }

// waking 1.5 ms late every block must not slow the long-run rate
TEST(block_clock_oversleep_does_not_accumulate) {
  BlockClock c;
  const double sr = 48000;
  const int frames = 240;
  clk::time_point t0 = clk::now();
  clk::time_point now = t0;
  long total = 0;
  for (int i = 0; i < 2000; ++i) {
    total += frames;
    clk::time_point next = c.next(frames, sr, now);
    now = std::max(now, next) + microseconds(1500);
  }
  double rate = total / seconds(now - t0);
  CHECK(std::fabs(rate / sr - 1.0) < 0.001);
}

TEST(block_clock_deadlines_are_exact) {
  BlockClock c;
  clk::time_point t0 = clk::now();
  clk::time_point d = t0;
  for (int i = 0; i < 1875; ++i) d = c.next(256, 48000, i == 0 ? t0 : d);
  // 1875 blocks of 256 frames is exactly 10 s
  CHECK(std::fabs(seconds(d - t0) - 10.0) < 1e-6);
}

// a long stall (overload, debugger) resyncs instead of bursting to catch up
TEST(block_clock_resyncs_after_stall) {
  BlockClock c;
  clk::time_point t0 = clk::now();
  c.next(240, 48000, t0);
  clk::time_point late = t0 + milliseconds(500);
  clk::time_point next = c.next(240, 48000, late);
  CHECK(next >= late);
  CHECK(next - late <= milliseconds(6));
}

TEST(block_clock_reset_and_rate_change_rebase) {
  BlockClock c;
  clk::time_point t0 = clk::now();
  c.next(240, 48000, t0);
  c.next(240, 48000, t0);
  clk::time_point t1 = t0 + milliseconds(3);
  // a new rate restarts from now rather than reinterpreting past frames
  clk::time_point next = c.next(441, 44100, t1);
  CHECK(std::fabs(seconds(next - t1) - 0.01) < 1e-6);
  c.reset();
  clk::time_point t2 = t0 + milliseconds(100);
  next = c.next(240, 48000, t2);
  CHECK(std::fabs(seconds(next - t2) - 0.005) < 1e-6);
}

TEST(block_clock_frames_for_rate) {
  CHECK(BlockClock::blockFrames(48000) == 240);
  CHECK(BlockClock::blockFrames(44100) == 221);
  CHECK(BlockClock::blockFrames(1000) == 16);
}
