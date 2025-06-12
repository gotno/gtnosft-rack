#include "rack.hpp"

#include <map>
#include <set>

#include "../util/Timer.hpp"

class OSCctrlWidget;
class OscSender;
class ChunkedManager;

enum SubscriptionType {
  LIGHTS,
};

struct SubscriptionManager {
  SubscriptionManager(
    OSCctrlWidget* ctrl,
    OscSender* sender,
    ChunkedManager* chunkman
  );
  ~SubscriptionManager();

  void start();
  void reset();

  void subscribeModuleLights(int64_t moduleId);
  // void unsubscribeModuleLights(int64_t moduleId);

private:
  OSCctrlWidget* ctrl{NULL};
  OscSender* osctx{NULL};
  ChunkedManager* chunkman{NULL};

  std::map<SubscriptionType, std::atomic<bool>> inFlight;
  std::set<int64_t> moduleLightSubs;

  Interval sendInterval;
};
