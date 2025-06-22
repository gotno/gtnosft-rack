#pragma once

#include "Bundler.hpp"

struct ParamState {
  ParamState(int _id, rack::app::ParamWidget* widget):
    id(_id),
    visible(widget->isVisible()),
    value(widget->getParamQuantity()->getValue()),
    label(widget->getParamQuantity()->getString()) {}

  int id;
  bool visible;
  float value;
  std::string label;

  bool update(rack::app::ParamWidget* widget) {
    bool valueUnchanged = value == widget->getParamQuantity()->getValue();
    if (visible == widget->isVisible() && valueUnchanged)
      return false;

    visible = widget->isVisible();
    value = widget->getParamQuantity()->getValue();
    label = widget->getParamQuantity()->getString();

    // tooltip: pq->getString /n pq->getDescription

    return true;
  }
};

// TODO: rename ModuleParamsStateBundler
struct ModuleParamsBundler : Bundler {
  typedef std::list<std::pair<rack::app::ParamWidget*, ParamState>> ParamList;
  inline static std::map<int64_t, ParamList> params;

  ModuleParamsBundler(const std::vector<int64_t>& moduleIds);

  // callback to notify a subscription update is no longer in flight
  ModuleParamsBundler(
    const std::vector<int64_t>& subscribedModuleIds,
    std::function<void()> callback
  );

  void process(const std::vector<int64_t>& moduleIds);

  void collectParams(int64_t moduleId);
  void addMessage(int64_t moduleId, const ParamState& state);
};
