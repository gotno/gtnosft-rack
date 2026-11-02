#include "SubscriptionManager.hpp"

#include "../Ctrl.hpp"
#include "OscSender.hpp"
#include "ChunkedManager.hpp"

#include "Bundler/ModuleLightsBundler.hpp"
#include "Bundler/ModuleParamsBundler.hpp"

SubscriptionManager::SubscriptionManager(
  CtrlWidget* _ctrl, OscSender* _osctx, ChunkedManager* _chunkman
): ctrl(_ctrl), osctx(_osctx), chunkman(_chunkman) {}

SubscriptionManager::~SubscriptionManager() {}

void SubscriptionManager::start() {
  running = true;
}

void SubscriptionManager::tick() {
  if (!running) return;
  if (moduleLightSubs.empty()) return;

  osctx->submitLights(new ModuleLightsBundler(moduleLightSubs));
}

void SubscriptionManager::stop() {
  running = false;
}

void SubscriptionManager::reset() {
  moduleLightSubs.clear();

  // drop any update tick() posted while stop() landed
  osctx->drainMailboxes();

  ModuleLightsBundler::lights.clear();
  ModuleParamsBundler::params.clear();
}

bool SubscriptionManager::subscribeModuleLights(int64_t moduleId) {
  if (!APP->scene->rack->getModule(moduleId)) return false;

  auto sub = std::find(moduleLightSubs.begin(), moduleLightSubs.end(), moduleId);
  if (sub == moduleLightSubs.end()) moduleLightSubs.push_back(moduleId);
  return true;
}
// void unsubscribeModuleLights(int64_t moduleId);
