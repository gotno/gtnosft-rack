#pragma once

#include "Bundler.hpp"

struct LightState {
  LightState(int _id, rack::app::LightWidget* widget):
    id(_id),
    visible(widget->isVisible()),
    hexColor(rack::color::toHexString(widget->color)) {}

  int id;
  bool visible;
  std::string hexColor;

  bool update(rack::app::LightWidget* widget) {
    std::string currentHexColor = rack::color::toHexString(widget->color);
    if (visible == widget->isVisible() && hexColor == currentHexColor)
      return false;

    visible = widget->isVisible();
    hexColor = currentHexColor;
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
