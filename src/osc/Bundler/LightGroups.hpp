#pragma once

#include "rack.hpp"

#include <vector>

// Some modules stack several LightWidgets on the same spot instead of
// changing one light's color. Lights with the same box and paramId are
// grouped and reported as a single light. Group index == light id.
struct LightGroup {
  int32_t paramId{-1};
  // in draw order (bottom first)
  std::vector<rack::app::LightWidget*> widgets;

  rack::app::LightWidget* front() const { return widgets.front(); }

  // true if any member is visible
  bool isVisible() const;
  // screen blend of visible members' colors, as Rack's light layer does
  NVGcolor color() const;
  // source-over composite of visible members' bgColors (all if none visible)
  NVGcolor bgColor() const;
};

std::vector<LightGroup> collectLightGroups(rack::app::ModuleWidget* moduleWidget);
