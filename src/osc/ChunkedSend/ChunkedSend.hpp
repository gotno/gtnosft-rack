#pragma once

#include "plugin.hpp"
#include "../OscSender.hpp"

#include <map>
#include <vector>
#include <chrono>

class MessagePacker;

struct ChunkStatus {
  using time_point = std::chrono::_V2::steady_clock::time_point;

  ChunkStatus(int32_t _chunkNum): chunkNum(_chunkNum) {}

  int32_t chunkNum;
  uint8_t sends{0};
  bool acked{false};

  time_point firstSendTime;
  time_point ackTime;

  void registerSend() {
    if (sends == 0) firstSendTime =  std::chrono::steady_clock::now();
    ++sends;
  }

  void registerAcked() {
    if (!acked) ackTime = std::chrono::steady_clock::now();
    acked = true;
  }

  uint64_t getRoundTripDuration() {
    if (!acked) return 0;

    return std::chrono::duration_cast<std::chrono::microseconds>(
      ackTime - firstSendTime
    ).count();
  }
};

struct ChunkedSend {
  inline static int32_t idCounter{0};
  static const int32_t MAX_CHUNK_SIZE =
    OscSender::MSG_BUFFER_SIZE - sizeof(idCounter);

  ChunkedSend(uint8_t* _data, size_t _size);
  virtual ~ChunkedSend();

  // any chunk over max sends?
  // static const uint8_t MAX_SENDS = 3;
  // bool sendFailed();

  int32_t id;
  uint8_t* data;
  size_t size;
  int32_t numChunks;
  int32_t chunkSize = MAX_CHUNK_SIZE;

  std::map<int32_t, ChunkStatus> chunkStatuses;

  void init();
  void ack(int32_t chunkNum);
  void getUnackedChunkNums(std::vector<int32_t>& chunkNums);
  void registerChunkSent(int32_t chunkNum);
  ChunkStatus& getChunkStatus(int32_t chunkNum);

  virtual MessagePacker* getPackerForChunk(int32_t chunkNum);
};
