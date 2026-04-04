#pragma once

#include "Bundler.hpp"

struct LightState {
  LightState(int _id, rack::app::LightWidget* widget):
    id(_id),
    visible(widget->isVisible()),
    color(widget->color) {}

  int id;
  bool visible;
  NVGcolor color;

  bool update(rack::app::LightWidget* widget) {
    if (visible == widget->isVisible() && memcmp(&color, &widget->color, sizeof(NVGcolor)) == 0)
      return false;

    visible = widget->isVisible();
    color = widget->color;
    return true;
  }
};

// TODO: rename ModuleLightsStateBundler
struct ModuleLightsBundler : Bundler {
  typedef std::list<std::pair<rack::app::LightWidget*, LightState>> LightList;
  inline static std::map<int64_t, LightList> lights;

  ModuleLightsBundler(
    const std::vector<int64_t>& moduleIds,
    std::function<void()> callback
  );

  void collectLights(int64_t moduleId);
  void addMessage(int64_t moduleId, const LightState& state);
};
