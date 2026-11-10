#include "ModuleParamsBundler.hpp"
#include "ModuleCacheGuard.hpp"
#include "../../util/ParamVisibility.hpp"

#include <algorithm>

ModuleParamsBundler::ModuleParamsBundler(
  const std::vector<int64_t>& moduleIds
): Bundler("ModuleParamsBundler") {
  process(moduleIds);
}

ModuleParamsBundler::ModuleParamsBundler(
  const std::vector<int64_t>& subscribedModuleIds,
  std::function<void()> callback
): Bundler("ModuleParamsBundler") {
  beforeDestroy = callback;
  process(subscribedModuleIds);
}

void ModuleParamsBundler::process(const std::vector<int64_t>& moduleIds) {
  for (const auto& moduleId : moduleIds) {
    auto* moduleWidget = APP->scene->rack->getModule(moduleId);
    if (!moduleWidget) continue;

    // first time through, messages for all params are added as they are collected
    if (params.count(moduleId) == 0) {
      collectParams(moduleId);
      continue;
    }

    auto& paramList = params.at(moduleId);
    auto visibility = gtnosft::param_visibility::collect(moduleWidget);
    int64_t numUpdates = 0;
    for (auto it = paramList.begin(); it != paramList.end(); ++it) {
      rack::app::ParamWidget* widget = it->first;
      auto& state = it->second;

      if (state.update(widget, visibility.at(widget))) {
        addMessage(moduleId, state);
        ++numUpdates;
      }
    }

    messages.emplace_back(
      "/report/state/param/count",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << moduleId
          << (osc::int64)numUpdates
          ;
      }
    );
  }
}

void ModuleParamsBundler::collectParams(int64_t moduleId) {
  using namespace rack::app;
  using namespace rack::widget;

  ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  if (!moduleWidget) return;
  int paramId = 0;
  params.emplace(moduleId, ParamList());
  ModuleCacheGuard::ensure(moduleWidget, moduleId);
  auto& paramList = params.at(moduleId);
  auto visibility = gtnosft::param_visibility::collect(moduleWidget);

  for (ParamWidget* paramWidget : moduleWidget->getParams()) {
    paramId = paramWidget->getParamQuantity()->paramId;
    paramList.emplace_back(paramWidget,
      ParamState(paramId, paramWidget, visibility.at(paramWidget)));
    addMessage(moduleId, paramList.back().second);
  }

  messages.emplace_back(
    "/report/state/param/count",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << (osc::int64)paramList.size()
        ;
    }
  );
}

void ModuleParamsBundler::addMessage(
  int64_t moduleId,
  const ParamState& state
) {
  messages.emplace_back(
    "/set/state/param",
    [moduleId, state](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << state.id
        << state.visible
        << state.value
        << state.label.c_str()
        ;
    }
  );
}
