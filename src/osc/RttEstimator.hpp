#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

// Retransmission timeout from measured round trips, after RFC 6298:
// smoothed RTT plus four times its variation, clamped to [MIN_RTO, MAX_RTO].
// Feed it samples only from chunks sent exactly once (Karn's algorithm).
// Because resent chunks yield no samples, a timeout must back the RTO off
// itself (otherwise a link that slows down could never raise it again).
class RttEstimator {
public:
  using duration = std::chrono::nanoseconds;

  // RTT is clamped to min/max, min is naturally constrained by UI framerate
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
    rtoValue = std::clamp<duration>(
      duration((int64_t)(srttNs + 4.0 * rttvarNs)),
      MIN_RTO,
      MAX_RTO
    );
  }

  // doubles the RTO, up to MAX_RTO, until the next sample. call at most once
  // per resend pass in which a chunk timed out on its first send.
  void backoff() {
    std::lock_guard<std::mutex> lock(mutex);
    rtoValue = std::min<duration>(rtoValue * 2, MAX_RTO);
  }

  // MAX_RTO until the first sample
  duration rto() {
    std::lock_guard<std::mutex> lock(mutex);
    return rtoValue;
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
    rtoValue = MAX_RTO;
  }

private:
  std::mutex mutex;
  bool hasSample{false};
  double srttNs{0.0};
  double rttvarNs{0.0};
  duration rtoValue{MAX_RTO};
};
