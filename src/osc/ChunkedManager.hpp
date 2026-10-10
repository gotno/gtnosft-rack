#pragma once

#include "rack.hpp"

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

  // toggled by /bench/prep_worker for A/B runs
  std::atomic<bool> prepOnWorker{true};

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

  void runPrepWorker();
  // runs init (flip/compress) and enqueues the send
  void prepare(const std::shared_ptr<ChunkedSend>& chunkedSend);
  bool processChunked(const std::shared_ptr<ChunkedSend>& chunkedSend);
  void enqueueChunk(const std::shared_ptr<ChunkedSend>& chunkedSend, int32_t chunkNum);
};
