#pragma once

#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>

namespace rack::app {
struct LightWidget;
struct ModuleWidget;
}

namespace gtnosft::light_overrides {

using OverrideMap = std::map<
  std::pair<std::string, std::string>,
  std::function<bool(rack::app::LightWidget*)>
>;

// matching lights are omitted from structure/state and drawn in the overlay.
extern const OverrideMap overlayLights;

inline bool samePosition(
  float xPos,
  float yPos,
  float expectedX,
  float expectedY
) {
  constexpr float tolerance = 0.001f;
  bool matchesX = std::fabs(xPos - expectedX) < tolerance;
  bool matchesY = std::fabs(yPos - expectedY) < tolerance;
  return matchesX && matchesY;
}

inline bool isOverlayLight(
  const OverrideMap& overrides, const std::string& plugin,
  const std::string& model, rack::app::LightWidget* light
) {
  auto entry = overrides.find({plugin, model});
  return entry != overrides.end() && entry->second(light);
}

bool isOverlayLight(
  rack::app::ModuleWidget* module,
  rack::app::LightWidget* light
);

} // namespace gtnosft::light_overrides
