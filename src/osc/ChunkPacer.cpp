#include "ChunkPacer.hpp"

#include <algorithm>

void ChunkPacer::setActive(bool on, clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  active = on;
  rate = 0;
  tokens = BURST_BYTES;
  lastRefill = now;
  lastStats = now;
  haveBaseline = false;
  jitter.clear();
}

void ChunkPacer::expireIfStale(clock::time_point now) {
  if (rate > 0 && now - lastStats >= STATS_TIMEOUT) rate = 0;
}

void ChunkPacer::refill(clock::time_point now) {
  if (rate <= 0) {
    tokens = BURST_BYTES;
  } else if (now > lastRefill) {
    double sec = std::chrono::duration<double>(now - lastRefill).count();
    tokens = std::min(BURST_BYTES, tokens + sec * rate * 1e6 / 8);
  }
  lastRefill = std::max(lastRefill, now);
}

bool ChunkPacer::jitterTrouble(float jitterMs, clock::time_point now) {
  jitter.emplace_back(now, jitterMs);
  while (!jitter.empty() && now - jitter.front().first > JITTER_WINDOW) jitter.pop_front();
  float low = jitterMs;
  for (const auto& [at, value] : jitter) low = std::min(low, value);
  return jitterMs > std::max(1.5f * low, low + 2.f);
}

void ChunkPacer::onStats(const AudioStats& stats, clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return;
  expireIfStale(now);
  lastStats = now;

  bool jittery = jitterTrouble(stats.jitterMs, now);
  if (
    !haveBaseline
      || stats.underruns < baseline.underruns
      || stats.lost < baseline.lost
  ) {
    baseline = stats;
    haveBaseline = true;
    return;
  }

  bool trouble = jittery
    || stats.underruns > baseline.underruns
    || stats.lost > baseline.lost;
  baseline = stats;

  refill(now);
  if (trouble) {
    rate = std::max(MIN_MBPS, (rate > 0 ? rate : MAX_MBPS) * DECREASE);
  } else if (rate > 0) {
    rate += INCREASE_MBPS;
    if (rate >= MAX_MBPS - 1e-6) rate = 0;
  }
}

ChunkPacer::clock::time_point ChunkPacer::nextSendTime(clock::time_point now) {
  if (!enabled.load(std::memory_order_relaxed)) return clock::time_point::min();
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return clock::time_point::min();
  expireIfStale(now);
  refill(now);
  if (rate <= 0 || tokens > 0) return clock::time_point::min();

  double bytesPerSec = rate * 1e6 / 8;
  double sec = std::max(-tokens, 1.0) / bytesPerSec;
  return now + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(sec));
}

void ChunkPacer::consume(size_t bytes, clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return;
  expireIfStale(now);
  refill(now);
  if (rate > 0) tokens -= (double)bytes;
}

double ChunkPacer::rateMbps(clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return 0;
  expireIfStale(now);
  return rate;
}
