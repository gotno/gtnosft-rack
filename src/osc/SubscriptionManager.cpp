#include "SubscriptionManager.hpp"

#include "OscConstants.hpp"

#include "../OSCctrl.hpp"
#include "OscSender.hpp"
#include "ChunkedManager.hpp"

#include "Bundler/ModuleLightsBundler.hpp"

SubscriptionManager::SubscriptionManager(
  OSCctrlWidget* _ctrl, OscSender* _osctx, ChunkedManager* _chunkman
): ctrl(_ctrl), osctx(_osctx), chunkman(_chunkman) {
  inFlight.emplace(SubscriptionType::LIGHTS, false);

  sendInterval = Timer::setInterval(SUBSCRIPTION_SEND_DELAY, [this] {
    if (!moduleLightSubs.empty() && !inFlight[SubscriptionType::LIGHTS].load()) {
      inFlight[SubscriptionType::LIGHTS].store(true);

      ctrl->enqueueAction([this]() {
        // TODO: osctx deque
        // osctx->enqueueBundlerPriority(
        osctx->enqueueBundler(
          new ModuleLightsBundler(
            std::vector(moduleLightSubs.begin(), moduleLightSubs.end()),
            [this]() {
              inFlight[SubscriptionType::LIGHTS].store(false);
            }
          )
        );
      });
    }
  });
}

SubscriptionManager::~SubscriptionManager() {}

void SubscriptionManager::start() {
  sendInterval.start();
}

void SubscriptionManager::reset() {
  sendInterval.clear();
  moduleLightSubs.clear();
  inFlight.clear();
}

void SubscriptionManager::subscribeModuleLights(int64_t moduleId) {
  moduleLightSubs.insert(moduleId);
}
// void unsubscribeModuleLights(int64_t moduleId);
