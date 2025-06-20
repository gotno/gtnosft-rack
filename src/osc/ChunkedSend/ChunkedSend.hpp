#pragma once

#include "rack.hpp"

#include "../OscSender.hpp"

#include <map>
#include <mutex>
#include <vector>
#include <chrono>

class ChunkedManager;
class ChunkedSendBundler;

struct ChunkedSend {
  inline static int32_t idCounter{0};

  ChunkedSend(uint8_t* _data, int64_t _size);
  virtual ~ChunkedSend();

  virtual void init();

  using time_point = std::chrono::steady_clock::time_point;
  std::map<int32_t, time_point> chunkAckTimes;
  std::map<int32_t, time_point> chunkSendTimes;
  std::map<int32_t, uint8_t> chunkSendCounts;

  static const uint8_t MAX_SENDS = 5;
  std::atomic<bool> failed{false};
  bool sendFailed();
  bool sendSucceeded();

  int32_t id;
  uint8_t* data;
  int64_t size;
  int32_t numChunks{0};
  int32_t chunkSize{0};

  std::mutex statusMutex;

  void ack(int32_t chunkNum);
  bool acked(int32_t chunkNum);
  void getUnackedChunkNums(std::vector<int32_t>& chunkNums);
  void registerChunkSent(int32_t chunkNum);

  virtual ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) = 0;

  void logCompletionDuration(int32_t chunkNum);
  void logCompletionDuration();
};
