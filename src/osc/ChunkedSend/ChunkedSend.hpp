#pragma once

#include "rack.hpp"

#include "../OscSender.hpp"
#include "../../bench/Bench.hpp"

#include <atomic>
#include <mutex>
#include <optional>
#include <tuple>
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

  // fixed timeout used when adaptive retry is off
  static constexpr std::chrono::milliseconds RETRY_TIMEOUT{200};
  static const uint8_t MAX_RETRIES = 5;
  // adaptive retry gives up on a chunk unacked this long after its first send,
  // matching the fixed timeout's 6 sends
  static constexpr std::chrono::milliseconds GIVE_UP_AFTER{
    RETRY_TIMEOUT * (MAX_RETRIES + 1)
  };

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
  // a newer send of the same image replaced this one: stop sending it
  std::atomic<bool> superseded{false};
  bool sendFailed();
  bool sendSucceeded();

  // sends with equal keys carry frames of the same image (texture id and
  // size). a newer one supersedes the older. nullopt: never superseded
  using SupersedeKey = std::tuple<int64_t, int32_t, int32_t>;
  virtual std::optional<SupersedeKey> supersedeKey() const {
    return std::nullopt;
  }

  int64_t id;
  int32_t sequenceId{-1};
  uint8_t* data;
  int64_t size;
  int32_t numChunks{0};
  int32_t chunkSize{0};

  // written under statusMutex once the send is in flight
  BENCH(bench::TracePtr trace;)

  // false if the chunk was already acked (or doesn't exist).
  // on the ack that completes the send, sets rtt to the slowest round trip
  // among chunks sent exactly once, so their acks can't belong to a different
  // copy (Karn's algorithm); leaves it alone otherwise.
  bool ack(int32_t chunkNum, std::chrono::nanoseconds* rtt = nullptr);
  bool acked(int32_t chunkNum);

  // marks pending and timed-out chunks as queued and returns their numbers.
  // flags the send as failed if a chunk has exhausted its retries.
  // adaptive: a chunk is due after rto, doubling with each resend up to
  // maxRto, and fails after GIVE_UP_AFTER. otherwise: fixed RETRY_TIMEOUT
  // and MAX_RETRIES. returns how many chunks timed out on their first send
  // (adaptive only), for estimator backoff.
  int32_t takeChunksDue(
    std::vector<int32_t>& chunkNums,
    bool adaptive,
    std::chrono::nanoseconds rto,
    std::chrono::nanoseconds maxRto
  );
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
  // slowest Karn-valid round trip so far, reported when the send completes
  std::chrono::nanoseconds maxRtt{0};

  bool validChunk(int32_t chunkNum);
};
