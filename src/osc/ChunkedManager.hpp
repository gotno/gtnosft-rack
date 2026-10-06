#pragma once

#include "rack.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <utility>

class ChunkedSend;
class OscSender;

struct ChunkedManager {
  ChunkedManager(OscSender* sender);
  ~ChunkedManager();

  void add(ChunkedSend* chunked);
  void ack(int64_t id, int32_t sequenceId, int32_t chunkNum);
  void tick();

private:
  OscSender* osctx{NULL};

  using ChunkedKey = std::pair<int64_t, int32_t>;
  std::map<ChunkedKey, std::shared_ptr<ChunkedSend>> chunkedSends;
  std::mutex chunkedSendsMutex;

  bool processChunked(const std::shared_ptr<ChunkedSend>& chunkedSend);
  void enqueueChunk(const std::shared_ptr<ChunkedSend>& chunkedSend, int32_t chunkNum);
};
