#include "Bench.hpp"

#ifdef GTNOSFT_BENCH

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

namespace bench {

namespace {

struct TextureTally {
  std::string kind;
  int32_t completed{0};
  int32_t failed{0};
  time_point firstAck{};
  time_point lastAck{};
};

std::mutex mutex;
std::atomic<uint64_t> generation{1};
time_point windowStart = clock::now();
std::map<std::string, std::vector<float>> samples;
std::map<std::string, int64_t> counters;
std::map<std::string, std::pair<int64_t, int64_t>> gauges; // current, peak
std::map<int64_t, TextureTally> textures;

// UI thread only
TracePtr currentTrace;

const char* kindOf(const Trace& trace) {
  if (!trace.overlay) return "texture";
  return trace.cacheHit ? "overlay_hit" : "overlay_miss";
}

float ms(time_point from, time_point to) {
  return std::chrono::duration<float, std::milli>(to - from).count();
}

// caller holds mutex
void addSpan(
  const std::string& kind,
  const char* name,
  const Trace& trace,
  Stage from,
  Stage to
) {
  if (!trace.has(from) || !trace.has(to)) return;
  samples[kind + "." + name].push_back(ms(trace.at(from), trace.at(to)));
}

float percentile(const std::vector<float>& sorted, float p) {
  if (sorted.empty()) return 0.f;
  // nearest-rank
  size_t rank = (size_t)std::ceil(p * sorted.size());
  size_t idx = std::min(sorted.size() - 1, rank > 0 ? rank - 1 : 0);
  return sorted[idx];
}

} // namespace

TracePtr begin(int64_t textureId, int32_t sequenceId) {
  TracePtr trace = std::make_shared<Trace>();
  trace->generation = generation.load();
  trace->textureId = textureId;
  trace->sequenceId = sequenceId;
  trace->stamp(Stage::Received);
  return trace;
}

void setCurrent(TracePtr trace) { currentTrace = std::move(trace); }

Trace* current() { return currentTrace.get(); }

void stamp(Stage stage) {
  if (currentTrace) currentTrace->stamp(stage);
}

void submit(const Trace& trace) {
  std::lock_guard<std::mutex> lock(mutex);
  if (trace.generation != generation.load()) return;

  bool completed = !trace.failed && trace.has(Stage::AllAcked);
  std::string kind = kindOf(trace);

  counters[kind + ".requests"] += 1;
  counters[kind + (completed ? ".completed" : ".failed")] += 1;
  counters[kind + ".retries"] += trace.retries;
  counters[kind + ".rerenders"] += trace.rerenders;

  // overlays are recorded both by cache outcome and combined, so runs with
  // the cache on (all hits) and off (all misses) can be compared directly
  std::vector<std::string> spanKinds{kind};
  if (trace.overlay) spanKinds.push_back("overlay");
  for (const std::string& spanKind : spanKinds) {
    addSpan(spanKind, "queue_wait", trace, Stage::Received, Stage::Dequeued);
    addSpan(spanKind, "prepare", trace, Stage::RenderStart, Stage::Prepared);
    addSpan(spanKind, "draw", trace, Stage::Prepared, Stage::Drawn);
    addSpan(spanKind, "readback", trace, Stage::Drawn, Stage::ReadBack);
    addSpan(spanKind, "flip", trace, Stage::ReadBack, Stage::Flipped);
    addSpan(spanKind, "handoff", trace, Stage::Flipped, Stage::CompressStart);
    addSpan(spanKind, "compress", trace, Stage::CompressStart, Stage::Compressed);
    addSpan(spanKind, "send_queue", trace, Stage::Compressed, Stage::FirstChunkSent);
    addSpan(spanKind, "send_all", trace, Stage::FirstChunkSent, Stage::LastChunkSent);
    addSpan(spanKind, "ack_all", trace, Stage::FirstChunkSent, Stage::AllAcked);
    addSpan(spanKind, "render_total", trace, Stage::RenderStart, Stage::Flipped);
    addSpan(spanKind, "request_to_first_send", trace, Stage::Received, Stage::FirstChunkSent);
    addSpan(spanKind, "request_to_all_acked", trace, Stage::Received, Stage::AllAcked);

    if (trace.rawBytes > 0)
      samples[spanKind + ".raw_kb"].push_back(trace.rawBytes / 1024.f);
    if (trace.compressedBytes > 0)
      samples[spanKind + ".compressed_kb"].push_back(trace.compressedBytes / 1024.f);
    if (trace.numChunks > 0)
      samples[spanKind + ".chunks"].push_back((float)trace.numChunks);
  }

  TextureTally& tally = textures[trace.textureId];
  tally.kind = trace.overlay ? "overlay" : "texture";
  if (completed) {
    time_point ackedAt = trace.at(Stage::AllAcked);
    if (tally.completed == 0 || ackedAt < tally.firstAck) tally.firstAck = ackedAt;
    if (tally.completed == 0 || ackedAt > tally.lastAck) tally.lastAck = ackedAt;
    ++tally.completed;
  } else {
    ++tally.failed;
  }
}

void count(const std::string& name, int64_t delta) {
  std::lock_guard<std::mutex> lock(mutex);
  counters[name] += delta;
}

void gauge(const std::string& name, int64_t value) {
  std::lock_guard<std::mutex> lock(mutex);
  auto& [current, peak] = gauges[name];
  current = value;
  peak = std::max(peak, value);
}

void recordFrame(double frameIntervalSec, double ctrlStepSec) {
  std::lock_guard<std::mutex> lock(mutex);
  counters["frame.count"] += 1;
  if (std::isfinite(frameIntervalSec))
    samples["frame.interval"].push_back((float)(frameIntervalSec * 1000.0));
  samples["frame.ctrl_step"].push_back((float)(ctrlStepSec * 1000.0));
}

uint64_t reset() {
  std::lock_guard<std::mutex> lock(mutex);
  uint64_t newGeneration = ++generation;
  windowStart = clock::now();
  samples.clear();
  counters.clear();
  textures.clear();
  // keep current gauge values, restart peaks from them
  for (auto& [name, value] : gauges) value.second = value.first;
  return newGeneration;
}

Report report() {
  std::lock_guard<std::mutex> lock(mutex);

  Report report;
  report.generation = generation.load();
  report.windowSec =
    std::chrono::duration<float>(clock::now() - windowStart).count();

  for (auto& [name, values] : samples) {
    if (values.empty()) continue;
    std::vector<float> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (float v : sorted) sum += v;
    report.stats.push_back(Summary{
      name,
      (int32_t)sorted.size(),
      (float)(sum / sorted.size()),
      sorted.front(),
      percentile(sorted, 0.50f),
      percentile(sorted, 0.95f),
      percentile(sorted, 0.99f),
      sorted.back()
    });
  }

  for (auto& [name, value] : counters) report.counters.emplace_back(name, value);
  for (auto& [name, value] : gauges) {
    report.counters.emplace_back(name, value.first);
    report.counters.emplace_back(name + ".peak", value.second);
  }

  for (auto& [textureId, tally] : textures) {
    float fps = 0.f;
    if (tally.completed >= 2) {
      float spanSec =
        std::chrono::duration<float>(tally.lastAck - tally.firstAck).count();
      if (spanSec > 0.f) fps = (tally.completed - 1) / spanSec;
    }
    report.textures.push_back(TextureRate{
      textureId,
      tally.kind,
      tally.completed,
      tally.failed,
      fps
    });
  }

  return report;
}

} // namespace bench

#endif
