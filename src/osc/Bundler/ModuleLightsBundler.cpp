#include "ModuleLightsBundler.hpp"
#include "ModuleCacheGuard.hpp"

ModuleLightsBundler::ModuleLightsBundler(
  const std::vector<int64_t>& subscribedModuleIds
): Bundler("ModuleLightsBundler") {

  std::vector<LightEntry> updates;

  for (const auto& moduleId : subscribedModuleIds) {
    if (!APP->scene->rack->getModule(moduleId)) continue;

    if (lights.count(moduleId) == 0) {
      collectLights(moduleId, updates);
      continue;
    }

    auto& lightList = lights.at(moduleId);

    for (auto& [group, state] : lightList)
      if (state.update(group)) updates.emplace_back(moduleId, state);
  }

  for (size_t i = 0; i < updates.size(); i += MAX_ENTRIES) {
    size_t end = std::min(i + MAX_ENTRIES, updates.size());
    addMessage({updates.begin() + i, updates.begin() + end});
  }
}

void ModuleLightsBundler::collectLights(
  int64_t moduleId,
  std::vector<LightEntry>& updates
) {
  rack::app::ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  lights.emplace(moduleId, LightList());
  ModuleCacheGuard::ensure(moduleWidget, moduleId);
  auto& lightList = lights.at(moduleId);

  int lightId = 0;
  for (LightGroup& group : collectLightGroups(moduleWidget)) {
    LightState state(lightId++, group);
    lightList.emplace_back(std::move(group), state);
    updates.emplace_back(moduleId, state);
  }
}

void ModuleLightsBundler::addMessage(const std::vector<LightEntry>& batch) {
  messages.emplace_back(
    "/set/s/l",
    [batch](osc::OutboundPacketStream& pstream) {
      for (const auto& [moduleId, state] : batch) {
        pstream << moduleId
          << state.id
          << state.visible
          << state.color
          ;
      }
    }
  );
}
