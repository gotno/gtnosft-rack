#include "rack.hpp"

#include <map>
#include <memory>
#include <utility>

class OSCctrlWidget;
class ChunkedSend;
class OscSender;

struct ChunkedManager {
  ChunkedManager(OSCctrlWidget* ctrl, OscSender* sender);
  ~ChunkedManager();

  void add(ChunkedSend* chunked);
  void ack(int64_t id, int32_t sequenceId, int32_t chunkNum);

  void processChunked(int64_t id, int32_t sequenceId);

  // used by bundlers. returns null if not found.
  ChunkedSend* findChunked(int64_t id, int32_t sequenceId);

private:
  OSCctrlWidget* ctrl{NULL};
  OscSender* osctx{NULL};

  // keyed by (id, sequenceId)
  using ChunkedKey = std::pair<int64_t, int32_t>;
  std::map<ChunkedKey, std::unique_ptr<ChunkedSend>> chunkedSends;
  bool chunkedExists(int64_t id, int32_t sequenceId);

  // used internally, asserts chunked exists
  ChunkedSend* getChunked(int64_t id, int32_t sequenceId);
};
