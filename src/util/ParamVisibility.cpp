#include "ParamVisibility.hpp"

#include <rack.hpp>

namespace gtnosft::param_visibility {

const PolicyMap policies = {
  {
    {"Befaco", "Iroi"},
    [](rack::app::ModuleWidget*, Controls& controls) {
      stackedControls(controls);
    }
  },
};

Snapshot collect(rack::app::ModuleWidget* module) {
  auto* model = module->model;
  auto policy =
    model && model->plugin
      ? policies.find({model->plugin->slug, model->slug})
      : policies.end();
  Snapshot result;
  Controls controls;

  for (auto* param : module->getParams()) {
    bool visible = param->isVisible();

    if (policy == policies.end()) {
      result.emplace(param, visible);
      continue;
    }

    auto center = param->getRelativeOffset(param->box.size.div(2.f), module);
    for (auto* parent = param->parent; parent && parent != module; parent = parent->parent)
      visible = visible && parent->isVisible();
    controls.push_back({center.x, center.y, visible, param});
  }

  if (policy != policies.end()) {
    policy->second(module, controls);
    for (const auto& control : controls)
      result.emplace(control.widget, control.visible);
  }

  return result;
}

} // namespace gtnosft::param_visibility
