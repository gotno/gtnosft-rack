#pragma once

#include "rack.hpp"

#include "../OscSender.hpp"
#include "../../bench/Bench.hpp"

#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>

class ChunkedManager;
class ChunkedSendBundler;

struct ChunkedSend {
  inline static int64_t idCounter{0};

  ChunkedSend(uint8_t* _data, int64_t _size);
  virtual ~ChunkedSend();

  virtual void init();

  using clock = std::chrono::steady_clock;
  using time_point = clock::time_point;

  static constexpr std::chrono::milliseconds RETRY_TIMEOUT{200};
  static const uint8_t MAX_RETRIES = 5;

  enum class ChunkState : uint8_t {
    Pending, // waiting to be enqueued
    Queued, // bundler is in the queue
    Sent, // in flight, awaiting ack or timeout
    Acked,
  };

  struct ChunkStatus {
    ChunkState state{ChunkState::Pending};
    uint8_t sendCount{0};
    time_point firstSentAt{};
    time_point lastSentAt{};
    time_point ackedAt{};
  };

  std::atomic<bool> failed{false};
  bool sendFailed();
  bool sendSucceeded();

  int64_t id;
  int32_t sequenceId{-1};
  uint8_t* data;
  int64_t size;
  int32_t numChunks{0};
  int32_t chunkSize{0};

  // written under statusMutex once the send is in flight
  BENCH(bench::TracePtr trace;)

  // false if the chunk was already acked (or doesn't exist)
  bool ack(int32_t chunkNum);
  bool acked(int32_t chunkNum);

  // marks pending and timed-out chunks as queued and returns their numbers.
  // flags the send as failed if a chunk has exhausted its retries.
  void takeChunksDue(std::vector<int32_t>& chunkNums);
  void registerChunkSent(int32_t chunkNum);
  // safeguard to prevent chunks getting stuck
  void registerChunkDropped(int32_t chunkNum);

  virtual ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) = 0;

  void logCompletionDuration(int32_t chunkNum);
  void logCompletionDuration();

private:
  std::mutex statusMutex;
  std::vector<ChunkStatus> chunks;
  int32_t numAcked{0};

  bool validChunk(int32_t chunkNum);
};
