#include "rack.hpp"

#include <map>
#include <memory>

class OSCctrlWidget;
class ChunkedSend;
class OscSender;

struct ChunkedManager {
  ChunkedManager(OSCctrlWidget* ctrl, OscSender* sender);
  ~ChunkedManager();

  void add(ChunkedSend* chunked, bool deferIfAlreadyQueued = false);
  void ack(int32_t id, int32_t chunkNum);

  bool isProcessing(int32_t id);
  void processChunked(int32_t id);

  // used by bundlers. returns null if not found.
  ChunkedSend* findChunked(int32_t id);

private:
  OSCctrlWidget* ctrl{NULL};
  OscSender* osctx{NULL};

  std::map<int32_t, std::unique_ptr<ChunkedSend>> chunkedSends;
  bool chunkedExists(int32_t id);

  void defer(ChunkedSend* chunked);
  std::map<int32_t, ChunkedSend*> deferredSends;
  bool deferredExists(int32_t id);

  // used internally, asserts chunked exists
  ChunkedSend* getChunked(int32_t id);
};
