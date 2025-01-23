#include "plugin.hpp"

#include <map>

class ChunkedSend;
class OscSender;

struct ChunkedManager {
  ChunkedManager(OscSender* sender);
  ~ChunkedManager();

  void add(ChunkedSend* chunked);
  void ack(int32_t id, int32_t chunkNum);
  bool isProcessing(int32_t id);

private:
  OscSender* osctx{NULL};

  std::map<int32_t, ChunkedSend*> chunkedSends;

  void processChunked(int32_t id);
  void reprocessChunked(int32_t id);
  bool chunkedExists(int32_t id);
  ChunkedSend* getChunked(int32_t id);
};
