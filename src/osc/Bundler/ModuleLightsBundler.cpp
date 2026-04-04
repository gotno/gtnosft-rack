#include "ModuleLightsBundler.hpp"

#include <unordered_set>

ModuleLightsBundler::ModuleLightsBundler(
  const std::vector<int64_t>& subscribedModuleIds,
  std::function<void()> callback
): Bundler("ModuleLightsBundler") {
  beforeDestroy = callback;

  // filter subscriptions to loaded modules
  std::vector<int64_t> rackModuleIds = APP->engine->getModuleIds();
  std::unordered_set<int64_t> rackModuleSet(rackModuleIds.begin(), rackModuleIds.end());

  for (const auto& moduleId : subscribedModuleIds) {
    if (!rackModuleSet.contains(moduleId)) continue;

    if (lights.count(moduleId) == 0) {
      collectLights(moduleId);
      continue;
    }

    auto& lightList = lights.at(moduleId);

    for (auto it = lightList.begin(); it != lightList.end(); ++it) {
      rack::app::LightWidget* widget = it->first;
      auto& state = it->second;

      if (state.update(widget)) addMessage(moduleId, state);
    }
  }
}

void ModuleLightsBundler::collectLights(int64_t moduleId) {
  using namespace rack::app;
  using namespace rack::widget;

  ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  int lightId = 0;
  lights.emplace(moduleId, LightList());
  auto& lightList = lights.at(moduleId);

  // panel lights
  for (Widget* widget : moduleWidget->children) {
    if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget)) {
      lightList.emplace_back(lightWidget, LightState(lightId, lightWidget));
      addMessage(moduleId, lightList.back().second);
      ++lightId;
    }
  }

  // param lights
  for (ParamWidget* & paramWidget : moduleWidget->getParams()) {
    for (Widget* & widget : paramWidget->children) {
      if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget)) {
        lightList.emplace_back(lightWidget, LightState(lightId, lightWidget));
        addMessage(moduleId, lightList.back().second);
        ++lightId;
      }
    }
  }
}

void ModuleLightsBundler::addMessage(
  int64_t moduleId,
  const LightState& state
) {
  messages.emplace_back(
    "/set/s/l",
    [moduleId, state](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << state.id
        << state.visible
        << rack::color::toHexString(state.color).c_str()
        ;
    }
  );
}
