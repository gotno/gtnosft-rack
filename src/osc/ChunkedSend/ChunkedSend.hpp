#pragma once

#include "plugin.hpp"
#include "../OscSender.hpp"

#include <map>
#include <mutex>
#include <vector>
#include <chrono>

class MessagePacker;

struct ChunkedSend {
  inline static int32_t idCounter{0};
  static const int32_t MAX_CHUNK_SIZE =
    OscSender::MSG_BUFFER_SIZE - sizeof(idCounter);
  static const int32_t BATCH_SIZE{10};

  ChunkedSend(uint8_t* _data, int64_t _size);
  virtual ~ChunkedSend();

  using time_point = std::chrono::_V2::steady_clock::time_point;
  std::map<int32_t, time_point> chunkAckTimes;
  std::map<int32_t, time_point> chunkSendTimes;
  std::map<int32_t, uint8_t> chunkSendCounts;

  static const uint8_t MAX_SENDS = 5;
  bool sendFailed();
  bool sendSucceeded();

  int32_t id;
  uint8_t* data;
  int64_t size;
  int32_t numChunks;
  int32_t chunkSize = MAX_CHUNK_SIZE;

  std::mutex statusMutex;

  void calculateNumChunks();
  void ack(int32_t chunkNum);
  void getUnackedChunkNums(std::vector<int32_t>& chunkNums);
  void registerChunkSent(int32_t chunkNum);

  virtual MessagePacker* getPackerForChunk(int32_t chunkNum);

  void logCompletionDuration(int32_t chunkNum);
  void logCompletionDuration();
};
