#include "ModuleLightsBundler.hpp"

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

    for (auto& [widget, state] : lightList)
      if (state.update(widget)) updates.emplace_back(moduleId, state);
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
      updates.emplace_back(moduleId, lightList.back().second);
      ++lightId;
    }
  }

  // param lights
  for (ParamWidget* & paramWidget : moduleWidget->getParams()) {
    for (Widget* & widget : paramWidget->children) {
      if (LightWidget* lightWidget = dynamic_cast<LightWidget*>(widget)) {
        lightList.emplace_back(lightWidget, LightState(lightId, lightWidget));
        updates.emplace_back(moduleId, lightList.back().second);
        ++lightId;
      }
    }
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
