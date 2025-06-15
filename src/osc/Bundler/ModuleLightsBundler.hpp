#pragma once

#include "Bundler.hpp"

struct ComponentState{
  ComponentState(int _id, bool _visible): id(_id), visible(_visible) {}

  int id;
  bool visible;
};

struct LightState: public ComponentState {
  LightState(int _id, rack::app::LightWidget* widget):
    ComponentState(_id, widget->isVisible()), color(widget->color) {}

  NVGcolor color;

  bool changed(rack::app::LightWidget* widget) {
    if (widget->isVisible() != visible) return true;
    if (!rack::color::isEqual(widget->color, color)) return true;
    return false;
  }

  void update(rack::app::LightWidget* widget) {
    visible = widget->isVisible();
    color = widget->color;
  }
};

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
