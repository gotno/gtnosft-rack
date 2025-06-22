#include "ModuleParamsBundler.hpp"

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
  // filter subscriptions to loaded modules
  std::vector<int64_t> rackModuleIds = APP->engine->getModuleIds();
  std::sort(rackModuleIds.begin(), rackModuleIds.end());

  std::vector<int64_t> moduleIdsToProcess(rackModuleIds.size());
  auto it = std::set_intersection(
    moduleIds.begin(),
    moduleIds.end(),
    rackModuleIds.begin(),
    rackModuleIds.end(),
    moduleIdsToProcess.begin()
  );
  moduleIdsToProcess.resize(it - moduleIdsToProcess.begin());

  for (const auto& moduleId : moduleIdsToProcess) {
    if (params.count(moduleId) == 0) {
      collectParams(moduleId);
      continue;
    }

    auto& paramList = params.at(moduleId);

    for (auto it = paramList.begin(); it != paramList.end(); ++it) {
      rack::app::ParamWidget* widget = it->first;
      auto& state = it->second;

      if (state.update(widget)) addMessage(moduleId, state);
    }
  }
}

void ModuleParamsBundler::collectParams(int64_t moduleId) {
  using namespace rack::app;
  using namespace rack::widget;

  ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  int paramId = 0;
  params.emplace(moduleId, ParamList());
  auto& paramList = params.at(moduleId);

  for (ParamWidget* paramWidget : moduleWidget->getParams()) {
    paramId = paramWidget->getParamQuantity()->paramId;
    paramList.emplace_back(paramWidget, ParamState(paramId, paramWidget));
    addMessage(moduleId, paramList.back().second);
  }
}

void ModuleParamsBundler::addMessage(
  int64_t moduleId,
  const ParamState& state
) {
  messages.emplace_back(
    "/set/s/p",
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
