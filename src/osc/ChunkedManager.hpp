#include "plugin.hpp"

#include <map>

class ChunkedSend;
class OscSender;

struct ChunkedManager {
  ChunkedManager(OscSender* sender);
  ~ChunkedManager();

  OscSender* osctx{NULL};

  std::map<int32_t, ChunkedSend*> chunkedSends;

  void add(ChunkedSend* chunked);
  void processChunked(int32_t id);
  ChunkedSend* getChunked(int32_t id);
  // void later();
};
