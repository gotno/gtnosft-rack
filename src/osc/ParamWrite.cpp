#include "ParamWrite.hpp"
#include "ParamInteraction.hpp"
#include "Bundler/ModuleCacheGuard.hpp"

#include <rack.hpp>
#include <map>
#include <utility>

namespace gtnosft::param_write {
namespace {

using ParamKey = std::pair<int64_t, int>;
std::map<ParamKey, EncoderState> encoders;

struct ScopedKnobSettings {
  rack::app::Knob& knob;
  rack::engine::ParamQuantity& quantity;
  bool forceLinear, smoothEnabled;

  ScopedKnobSettings(
    rack::app::Knob& knob,
    rack::engine::ParamQuantity& quantity
  ):
    knob(knob),
    quantity(quantity),
    forceLinear(knob.forceLinear),
    smoothEnabled(quantity.smoothEnabled) {
      knob.forceLinear = true;
      quantity.smoothEnabled = false;
    }

  ~ScopedKnobSettings() {
    knob.forceLinear = forceLinear;
    quantity.smoothEnabled = smoothEnabled;
  }
};

} // namespace

Result applyOverride(rack::app::ParamWidget* param, float value) {
  if (
    !param ||
    !param->module ||
    !param->module->model ||
    !param->module->model->plugin
  ) return Result::Failed;

  auto* model = param->module->model;
  const auto* entry = findOverride(model->plugin->slug, model->slug, param->paramId);
  if (!entry) return Result::NotOverridden;

  auto fail = [&](const char* reason) {
    WARN(
      "param write override %s:%s param %d: %s",
      model->plugin->slug.c_str(), model->slug.c_str(), param->paramId, reason
    );
    return Result::Failed;
  };

  auto* knob = dynamic_cast<rack::app::Knob*>(param);
  auto* quantity = param->getParamQuantity();

  if (
    !knob ||
    !quantity ||
    quantity->isBounded() ||
    quantity->snapEnabled ||
    knob->horizontal
  ) return fail("expected an unbounded, unsnapped, vertical knob");

  if (!APP->event) return fail("UI event state is unavailable");

  auto* moduleWidget = param->getAncestorOfType<rack::app::ModuleWidget>();
  if (!moduleWidget) return fail("parameter is not attached to a module widget");

  ParamKey key{param->module->id, param->paramId};

  float actual = quantity->getValue();
  auto existing = encoders.find(key);
  EncoderState next =
    existing == encoders.end()
      ? EncoderState(actual, entry->stepSize)
      : existing->second;
  next.observe(actual);
  if (!next.setTarget(value))
    return fail("invalid value or unrepresentable program/bank step count");

  if (next.appliedValue != actual) {
    if (
      !std::isfinite(rack::settings::knobLinearSensitivity) ||
      !std::isfinite(knob->speed)
    ) return fail("invalid drag sensitivity");

    ScopedKnobSettings transaction(*knob, *quantity);
    if (!gtnosft::param_interaction::withDrag(knob, [&]() {
      rack::widget::EventContext context;
      context.target = knob;
      rack::widget::Widget::DragMoveEvent event;
      event.context = &context;
      event.button = GLFW_MOUSE_BUTTON_LEFT;
      // NoisePlethora activates dragging after horizontal movement, without
      // changing the vertical encoder value or triggering Knob's click Action.
      event.mouseDelta = rack::math::Vec(16.f, 0.f);
      knob->onDragMove(event);
      quantity->setImmediateValue(next.appliedValue);
    })) return fail("another pointer interaction is active or the drag was rejected");
  }

  ModuleCacheGuard::ensure(moduleWidget, param->module->id);
  encoders.insert_or_assign(key, next);
  return Result::Applied;
}

float getValue(rack::app::ParamWidget* param) {
  float actual = param->getParamQuantity()->getValue();
  if (!param->module) return actual;

  auto entry = encoders.find({param->module->id, param->paramId});
  if (entry == encoders.end()) return actual;

  bool nativeDrag = APP->event && APP->event->getDraggedWidget() == param;
  entry->second.observe(actual, nativeDrag);
  return entry->second.logicalValue;
}

void forgetModule(int64_t moduleId) {
  std::erase_if(
    encoders,
    [=](const auto& entry) {
      return entry.first.first == moduleId;
    }
  );
}

void clear() {
  encoders.clear();
}

} // namespace gtnosft::param_write
