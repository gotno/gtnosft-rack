#include "SubscriptionManager.hpp"

#include "../OSCctrl.hpp"
#include "OscSender.hpp"
#include "ChunkedManager.hpp"

#include "Bundler/ModuleLightsBundler.hpp"
#include "Bundler/ModuleParamsBundler.hpp"

SubscriptionManager::SubscriptionManager(
  OSCctrlWidget* _ctrl, OscSender* _osctx, ChunkedManager* _chunkman
): ctrl(_ctrl), osctx(_osctx), chunkman(_chunkman) {}

SubscriptionManager::~SubscriptionManager() {}

void SubscriptionManager::start() {
  running = true;
  inFlight.emplace(SubscriptionType::LIGHTS, false);
}

void SubscriptionManager::tick() {
  if (!running) return;

  if (!moduleLightSubs.empty() && !inFlight[SubscriptionType::LIGHTS].load()) {
    inFlight[SubscriptionType::LIGHTS].store(true);

    // TODO: osctx deque for priority messages?
    // osctx->enqueueBundlerPriority(
    osctx->enqueueBundler(
      new ModuleLightsBundler(
        std::vector(moduleLightSubs.begin(), moduleLightSubs.end()),
        [this]() { inFlight[SubscriptionType::LIGHTS].store(false); }
      )
    );
  }
}

void SubscriptionManager::reset() {
  moduleLightSubs.clear();
  inFlight.clear();

  running = false;

  // clear cache after any other enqueued items
  ctrl->enqueueAction([]() {
    ModuleLightsBundler::lights.clear();
    // ModuleParamsBundler::params.clear();
  });
}

bool SubscriptionManager::subscribeModuleLights(int64_t moduleId) {
  if (!APP->scene->rack->getModule(moduleId)) return false;
  moduleLightSubs.insert(moduleId);
  return true;
}
// void unsubscribeModuleLights(int64_t moduleId);
