#include "test.hpp"
#include "osc/ChunkPacer.hpp"

#include <cmath>

using clk = ChunkPacer::clock;
using std::chrono::milliseconds;
using std::chrono::microseconds;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

static ChunkPacer::AudioStats stats(int underruns, int lost = 0, float jitter = 1.f) {
  return {underruns, lost, 0, jitter, 40.f};
}

// active, baselined at t0 + 500 ms, then one underrun at t0 + 1 s: 280 Mbit/s
static clk::time_point limited(ChunkPacer& p, clk::time_point t0) {
  p.setActive(true, t0);
  p.onStats(stats(0), t0 + milliseconds(500));
  p.onStats(stats(1), t0 + milliseconds(1000));
  return t0 + milliseconds(1000);
}

TEST(pacer_unlimited_at_start) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  CHECK(p.nextSendTime(t0) == clk::time_point::min());
  CHECK(near(p.rateMbps(t0), 0));
  p.setActive(true, t0);
  for (int i = 0; i < 100; ++i) p.consume(1452, t0);
  CHECK(p.nextSendTime(t0) == clk::time_point::min());
}

TEST(pacer_decreases_on_underrun_and_loss) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  CHECK(near(p.rateMbps(t), 280));
  p.onStats(stats(1, 2), t + milliseconds(500));
  CHECK(near(p.rateMbps(t + milliseconds(500)), 196));
}

TEST(pacer_floor) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 2; i < 20; ++i) {
    t += milliseconds(500);
    p.onStats(stats(i), t);
  }
  CHECK(near(p.rateMbps(t), 20));
}

TEST(pacer_additive_increase_reaches_unlimited) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 0; i < 11; ++i) {
    t += milliseconds(500);
    p.onStats(stats(1), t);
  }
  CHECK(near(p.rateMbps(t), 390));
  t += milliseconds(500);
  p.onStats(stats(1), t);
  CHECK(near(p.rateMbps(t), 0));
}

TEST(pacer_stale_stats_unlimit) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  CHECK(near(p.rateMbps(t + milliseconds(1900)), 280));
  CHECK(near(p.rateMbps(t + milliseconds(2100)), 0));
  // no backlog of waiting once unlimited
  for (int i = 0; i < 50; ++i) p.consume(1452, t + milliseconds(2100));
  CHECK(p.nextSendTime(t + milliseconds(2100)) == clk::time_point::min());
}

TEST(pacer_burst_then_wait) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 0; i < 8; ++i) {
    CHECK(p.nextSendTime(t) == clk::time_point::min());
    p.consume(1452, t);
  }
  CHECK(p.nextSendTime(t) != clk::time_point::min());
}

TEST(pacer_wait_time) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 0; i < 9; ++i) p.consume(1452, t);
  // 1452 bytes behind at 280 Mbit/s = 35 MB/s: about 41.5 us
  clk::time_point at = p.nextSendTime(t);
  long us = (long)std::chrono::duration_cast<microseconds>(at - t).count();
  CHECK(us >= 40 && us <= 43);
  CHECK(p.nextSendTime(t + microseconds(50)) == clk::time_point::min());
}

TEST(pacer_jitter_trouble) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  p.setActive(true, t0);
  clk::time_point t = t0;
  for (int i = 0; i < 4; ++i) {
    t += milliseconds(500);
    p.onStats(stats(0, 0, 1.f), t);
  }
  t += milliseconds(500);
  p.onStats(stats(0, 0, 2.9f), t);  // under max(1.5, 3.0)
  CHECK(near(p.rateMbps(t), 0));
  t += milliseconds(500);
  p.onStats(stats(0, 0, 5.f), t);
  CHECK(near(p.rateMbps(t), 280));
}

TEST(pacer_rebaselines_when_counters_drop) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  p.setActive(true, t0);
  p.onStats(stats(5, 3), t0 + milliseconds(500));
  p.onStats(stats(0, 0), t0 + milliseconds(1000));
  CHECK(near(p.rateMbps(t0 + milliseconds(1000)), 0));
  p.onStats(stats(0, 0), t0 + milliseconds(1500));
  CHECK(near(p.rateMbps(t0 + milliseconds(1500)), 0));
  p.onStats(stats(1, 0), t0 + milliseconds(2000));
  CHECK(near(p.rateMbps(t0 + milliseconds(2000)), 280));
}

TEST(pacer_inactive_or_disabled_is_unlimited) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  p.onStats(stats(0), t0);
  p.onStats(stats(3), t0 + milliseconds(500));
  CHECK(near(p.rateMbps(t0 + milliseconds(500)), 0));

  clk::time_point t = limited(p, t0 + milliseconds(1000));
  for (int i = 0; i < 9; ++i) p.consume(1452, t);
  p.enabled = false;
  CHECK(p.nextSendTime(t) == clk::time_point::min());
  p.enabled = true;
  CHECK(p.nextSendTime(t) != clk::time_point::min());
  p.setActive(false, t);
  CHECK(near(p.rateMbps(t), 0));
  CHECK(p.nextSendTime(t) == clk::time_point::min());
}
