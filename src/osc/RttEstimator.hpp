#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

// Retransmission timeout from measured round trips, after RFC 6298:
// smoothed RTT plus the larger of the timer granularity and four times the
// RTT variation, clamped to [MIN_RTO, MAX_RTO]. Resends are only checked once
// per UI frame, so the caller passes the frame interval as the granularity.
// Feed it one sample per send (its slowest chunk), only from chunks sent
// exactly once (Karn's algorithm). One sample per chunk would arrive as a
// tight burst each frame and collapse the variation, leaving no margin for
// frame-to-frame jitter (seen on Wi-Fi).
// Because resent chunks yield no samples, a timeout must back the RTO off
// itself (otherwise a link that slows down could never raise it again).
class RttEstimator {
public:
  using duration = std::chrono::nanoseconds;

  static constexpr std::chrono::milliseconds MIN_RTO{10};
  static constexpr std::chrono::milliseconds MAX_RTO{200};

  void addSample(duration rtt) {
    double sample = (double)rtt.count();
    std::lock_guard<std::mutex> lock(mutex);
    if (!hasSample) {
      srttNs = sample;
      rttvarNs = sample / 2.0;
      hasSample = true;
    } else {
      rttvarNs = 0.75 * rttvarNs + 0.25 * std::fabs(srttNs - sample);
      srttNs = 0.875 * srttNs + 0.125 * sample;
    }
    // a valid sample replaces any backed-off value
    backoffs = 0;
  }

  // doubles the RTO, up to MAX_RTO, until the next sample. call at most once
  // per resend pass in which a chunk timed out on its first send.
  void backoff() {
    std::lock_guard<std::mutex> lock(mutex);
    if (backoffs < MAX_BACKOFFS) ++backoffs;
  }

  // MAX_RTO until the first sample. granularity: how often resends are checked
  duration rto(duration granularity) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!hasSample) return MAX_RTO;
    double base = srttNs + std::max((double)granularity.count(), 4.0 * rttvarNs);
    return std::clamp<duration>(
      duration((int64_t)(base * (double)(1 << backoffs))),
      MIN_RTO,
      MAX_RTO
    );
  }

  // zero until the first sample
  duration srtt() {
    std::lock_guard<std::mutex> lock(mutex);
    return duration((int64_t)srttNs);
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex);
    hasSample = false;
    srttNs = rttvarNs = 0.0;
    backoffs = 0;
  }

private:
  std::mutex mutex;
  bool hasSample{false};
  double srttNs{0.0};
  double rttvarNs{0.0};
  // MIN_RTO doubled this many times exceeds MAX_RTO
  static constexpr int MAX_BACKOFFS = 5;
  int backoffs{0};
};
