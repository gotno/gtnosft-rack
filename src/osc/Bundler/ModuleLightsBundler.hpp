#pragma once

#include "Bundler.hpp"
#include <unordered_map>

struct LightState {
  LightState(int _id, rack::app::LightWidget* widget):
    id(_id),
    visible(widget->isVisible()),
    color(widget->color) {}

  int id;
  bool visible;
  NVGcolor color;

  bool update(rack::app::LightWidget* widget) {
    bool visibleChanged = visible != widget->isVisible();
    bool colorChanged = memcmp(&color, &widget->color, sizeof(NVGcolor)) != 0;
    if (!visibleChanged && !colorChanged) return false;

    visible = widget->isVisible();
    color = widget->color;
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
