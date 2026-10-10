#pragma once

// Benchmark instrumentation, compiled in only with -DGTNOSFT_BENCH (BENCH=1).
// Wrap call sites in BENCH(...) so they vanish from normal builds.

#ifdef GTNOSFT_BENCH

#define BENCH(...) __VA_ARGS__

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bench {

using clock = std::chrono::steady_clock;
using time_point = clock::time_point;

// in pipeline order
enum class Stage : uint8_t {
  Received, // OSC request parsed (receiver thread)
  Dequeued, // action picked up on the UI thread
  RenderStart,
  Prepared, // widget ready (overlay cache lookup/build, or fresh widget)
  Drawn, // FramebufferWidget::render + glFinish
  ReadBack, // glReadPixels
  PrepStart, // flip/compress begins (prep worker, or inline)
  Flipped,
  Compressed,
  FirstChunkSent,
  LastChunkSent, // every chunk has been sent at least once
  AllAcked,
  COUNT
};

struct Trace {
  uint64_t generation{0};
  int64_t textureId{-1};
  int32_t sequenceId{-1};
  bool overlay{false};
  bool cacheHit{false};
  bool failed{false};
  int32_t rerenders{0};
  int64_t rawBytes{0};
  int64_t compressedBytes{0};
  int32_t numChunks{0};
  int32_t chunksSent{0};
  int32_t retries{0};
  std::array<time_point, (size_t)Stage::COUNT> stamps{};

  void stamp(Stage stage) { stamps[(size_t)stage] = clock::now(); }
  bool has(Stage stage) const { return stamps[(size_t)stage] != time_point{}; }
  time_point at(Stage stage) const { return stamps[(size_t)stage]; }
};

using TracePtr = std::shared_ptr<Trace>;

// creates a trace stamped Received, tagged with the current generation
TracePtr begin(int64_t textureId, int32_t sequenceId);

// the trace whose request is being processed on the UI thread, if any
void setCurrent(TracePtr trace);
Trace* current();
void stamp(Stage stage);

// aggregate a finished trace (ignored if from before the last reset)
void submit(const Trace& trace);

void count(const std::string& name, int64_t delta = 1);
// tracks current value and peak
void gauge(const std::string& name, int64_t value);
void recordFrame(double frameIntervalSec, double ctrlStepSec);

// Lock-free count and busy time for hot paths (thousands of calls/sec) where
// a mutex per call would distort what's measured. Reported as counters
// `name` and `name.busy_us`. Look up once and keep the reference:
//   static bench::Tally& tally = bench::tally("rx.acks");
struct Tally {
  std::atomic<int64_t> count{0};
  std::atomic<int64_t> busyNs{0};

  void add(int64_t ns = 0) {
    count.fetch_add(1, std::memory_order_relaxed);
    if (ns) busyNs.fetch_add(ns, std::memory_order_relaxed);
  }
};

Tally& tally(const char* name);

// adds one call and its duration to a tally
class ScopedTally {
public:
  explicit ScopedTally(Tally& tally): tally(tally), start(clock::now()) {}
  ~ScopedTally() {
    tally.add(std::chrono::duration_cast<std::chrono::nanoseconds>(
      clock::now() - start
    ).count());
  }

private:
  Tally& tally;
  time_point start;
};

// starts a new measurement window, returning its generation
uint64_t reset();

struct Summary {
  std::string name;
  int32_t count;
  float mean, min, p50, p95, p99, max;
};

struct TextureRate {
  int64_t textureId;
  std::string kind;
  int32_t completed;
  int32_t failed;
  float fps;
};

struct Report {
  uint64_t generation;
  float windowSec;
  std::vector<Summary> stats;
  std::vector<std::pair<std::string, int64_t>> counters;
  std::vector<TextureRate> textures;
};

Report report();

} // namespace bench

#else

#define BENCH(...)

#endif
