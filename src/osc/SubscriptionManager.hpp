#include "rack.hpp"

#include <algorithm>
#include <vector>

class CtrlWidget;
class OscSender;
class ChunkedManager;

struct SubscriptionManager {
  SubscriptionManager(
    CtrlWidget* ctrl,
    OscSender* sender,
    ChunkedManager* chunkman
  );
  ~SubscriptionManager();

  void start();
  void reset();
  void tick();

  bool subscribeModuleLights(int64_t moduleId);
  // void unsubscribeModuleLights(int64_t moduleId);

private:
  CtrlWidget* ctrl{NULL};
  OscSender* osctx{NULL};
  ChunkedManager* chunkman{NULL};

  bool running{false};

  std::vector<int64_t> moduleLightSubs;
};
