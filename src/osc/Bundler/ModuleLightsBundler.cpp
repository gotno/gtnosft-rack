#include "ModuleLightsBundler.hpp"

#include <algorithm>

ModuleLightsBundler::ModuleLightsBundler(
  const std::vector<int64_t>& subscribedModuleIds,
  std::function<void()> callback
): Bundler("ModuleLightsBundler") {
  beforeDestroy = callback;

  // filter subscriptions to loaded modules
  std::vector<int64_t> rackModuleIds = APP->engine->getModuleIds();
  std::sort(rackModuleIds.begin(), rackModuleIds.end());

  std::vector<int64_t> moduleIds(rackModuleIds.size());
  auto it = std::set_intersection(
    subscribedModuleIds.begin(),
    subscribedModuleIds.end(),
    rackModuleIds.begin(),
    rackModuleIds.end(),
    moduleIds.begin()
  );
  moduleIds.resize(it - moduleIds.begin());

  for (const auto& moduleId : moduleIds) {
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
        << state.hexColor.c_str()
        ;
    }
  );
}
