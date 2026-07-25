#pragma once

#include "Bundler.hpp"
#include <unordered_map>

static inline int32_t packColor(NVGcolor c) {
  return (int32_t)(
      ((uint32_t)(c.a * 255.f + 0.5f) << 24)
    | ((uint32_t)(c.r * 255.f + 0.5f) << 16)
    | ((uint32_t)(c.g * 255.f + 0.5f) << 8)
    |  (uint32_t)(c.b * 255.f + 0.5f)
  );
}

struct LightState {
  LightState(int _id, rack::app::LightWidget* widget):
    id(_id),
    visible(widget->isVisible()),
    color(packColor(widget->color)) {}

  int id;
  bool visible;
  int32_t color;

  bool update(rack::app::LightWidget* widget) {
    bool visibleChanged = visible != widget->isVisible();
    int32_t newColor = packColor(widget->color);
    bool colorChanged = color != newColor;
    if (!visibleChanged && !colorChanged) return false;

    visible = widget->isVisible();
    color = newColor;
    return true;
  }
};

// TODO: rename ModuleLightsStateBundler
struct ModuleLightsBundler : Bundler {
  typedef std::list<std::pair<rack::app::LightWidget*, LightState>> LightList;
  inline static std::unordered_map<int64_t, LightList> lights;

  ModuleLightsBundler(
    const std::vector<int64_t>& moduleIds,
    std::function<void()> callback
  );

  void collectLights(int64_t moduleId);
  void addMessage(int64_t moduleId, const LightState& state);
};
