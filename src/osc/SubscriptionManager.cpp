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
}

void SubscriptionManager::tick() {
  if (!running) return;
  if (moduleLightSubs.empty()) return;

  osctx->submitLights(
    new ModuleLightsBundler(
      std::vector(moduleLightSubs.begin(), moduleLightSubs.end())
    )
  );
}

void SubscriptionManager::reset() {
  moduleLightSubs.clear();
  running = false;

  osctx->drainMailboxes();

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
