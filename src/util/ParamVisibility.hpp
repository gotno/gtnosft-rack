#pragma once

#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rack::app {
struct ModuleWidget;
struct ParamWidget;
}

namespace gtnosft::param_visibility {

struct Control {
  float centerX, centerY;
  bool visible;
  rack::app::ParamWidget* widget = nullptr;
};

using Controls = std::vector<Control>;
using PolicyMap = std::map<
  std::pair<std::string, std::string>,
  std::function<void(rack::app::ModuleWidget*, Controls&)>
>;
using Snapshot = std::map<rack::app::ParamWidget*, bool>;

extern const PolicyMap policies;

// controls are in draw order; only the topmost visible control owns its center
inline void stackedControls(Controls& controls) {
  constexpr float tolerance = 0.001f;
  for (size_t i = controls.size(); i > 0; --i) {
    auto& control = controls[i - 1];
    if (!control.visible) continue;

    for (size_t j = i; j < controls.size(); ++j) {
      const auto& above = controls[j];

      if (
        above.visible &&
        std::fabs(control.centerX - above.centerX) < tolerance &&
        std::fabs(control.centerY - above.centerY) < tolerance
      ) {
        control.visible = false;
        break;
      }
    }
  }
}

// Reporting only: does not modify Rack widgets or retain the snapshot.
Snapshot collect(rack::app::ModuleWidget* module);

} // namespace gtnosft::param_visibility
