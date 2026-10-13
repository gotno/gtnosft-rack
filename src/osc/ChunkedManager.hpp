#pragma once

#include "rack.hpp"

#include "RttEstimator.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

class ChunkedSend;
class OscSender;

struct ChunkedManager {
  ChunkedManager(OscSender* sender);
  ~ChunkedManager();

  // run prepare directly, or on the worker thread if prepOnWorker
  void add(ChunkedSend* chunked);
  void ack(int64_t id, int32_t sequenceId, int32_t chunkNum);
  void tick();
  void stopWorker();
  // true while queued prep and send work would take longer than a UI frame
  // to drain. texture renders wait while it holds and requests coalesce
  bool backedUp();
  // forget round-trip history, e.g. for a newly registered client
  void resetRtt() { rtt.reset(); }

  // toggled by /bench/prep_worker for A/B runs
  std::atomic<bool> prepOnWorker{true};
  // resend timeout from measured round trips, or the fixed RETRY_TIMEOUT.
  // toggled by /bench/adaptive_retry for A/B runs
  std::atomic<bool> adaptiveRetry{true};
  // toggled by /bench/back_pressure for A/B runs
  std::atomic<bool> backPressure{true};

private:
  OscSender* osctx{NULL};

  using ChunkedKey = std::pair<int64_t, int32_t>;
  std::map<ChunkedKey, std::shared_ptr<ChunkedSend>> chunkedSends;
  std::mutex chunkedSendsMutex;

  std::deque<std::shared_ptr<ChunkedSend>> prepQueue;
  std::mutex prepMutex;
  std::condition_variable prepCondition;
  bool prepStopping{false};
  std::thread prepWorker;
  // smoothed time per prepare() on the worker
  std::atomic<int64_t> prepNs{0};
  std::chrono::nanoseconds backlog();

  RttEstimator rtt;
  // written by tick() on the UI thread; read by prepare() on the worker too
  std::chrono::steady_clock::time_point lastTickAt{};
  std::atomic<int64_t> tickIntervalNs{16'000'000};
  std::chrono::nanoseconds tickInterval() const {
    return std::chrono::nanoseconds(tickIntervalNs.load(std::memory_order_relaxed));
  }
  static constexpr std::chrono::milliseconds MAX_TICK_INTERVAL{100};

  void runPrepWorker();
  // runs init (flip/compress) and enqueues the send
  void prepare(const std::shared_ptr<ChunkedSend>& chunkedSend);
  // firstTimeouts, if given, is incremented by the chunks that timed out on
  // their first send
  bool processChunked(
    const std::shared_ptr<ChunkedSend>& chunkedSend,
    std::chrono::nanoseconds rto,
    int32_t* firstTimeouts = nullptr
  );
  void enqueueChunk(const std::shared_ptr<ChunkedSend>& chunkedSend, int32_t chunkNum);
};
