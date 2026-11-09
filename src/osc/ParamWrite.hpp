#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace rack::app {
struct ParamWidget;
}

namespace gtnosft::param_write {

enum class Result {
  NotOverridden,
  Applied,
  Failed
};

struct Override {
  std::string_view plugin, model;
  int paramId;
  float stepSize;
};

inline const Override* findOverride(
  std::string_view plugin,
  std::string_view model,
  int paramId
) {
  static constexpr Override overrides[] = {
    {"Befaco", "NoisePlethora", 6, 0.125f}
  };

  for (const auto& entry : overrides) {
    if (entry.plugin == plugin && entry.model == model && entry.paramId == paramId)
      return &entry;
  }

  return nullptr;
}

enum class DragAction {
  Invalid,
  Unchanged,
  SetValue
};

inline DragAction planDrag(float current, float target, float stepSize) {
  if (
    !std::isfinite(current) ||
    !std::isfinite(target) ||
    !std::isfinite(stepSize) ||
    stepSize <= 0.f
  ) return DragAction::Invalid;

  // avoid overflowing the int some modules use for calculations
  double stepCount = std::fabs(double(target) - current) / stepSize;
  int limitSanityCheck = std::numeric_limits<int>::max() / 2;
  if (stepCount > limitSanityCheck) return DragAction::Invalid;

  return current == target ? DragAction::Unchanged : DragAction::SetValue;
}

struct EncoderState {
  float logicalValue;
  float appliedValue;
  float stepSize;

  EncoderState(float value, float stepSize):
    logicalValue(value), appliedValue(value), stepSize(stepSize) {}

  void observe(float actual, bool nativeDrag = false) {
    if (actual != appliedValue || nativeDrag)
      logicalValue = appliedValue = actual;
  }

  bool setTarget(float target) {
    if (planDrag(appliedValue, target, stepSize) == DragAction::Invalid)
      return false;

    double delta = (double(target) - appliedValue) / stepSize;
    double nearest = std::round(delta);
    double tolerance =
      std::numeric_limits<float>::epsilon() / double(stepSize) *
      (std::fabs(double(target)) + std::fabs(double(appliedValue)));

    // decimal client values can straddle an exact step by float roundoff
    if (tolerance < 0.5 && std::fabs(delta - nearest) <= tolerance)
      delta = nearest;

    double steps = std::trunc(delta);
    float next = float(double(appliedValue) + steps * stepSize);
    if (std::trunc((next - appliedValue) / stepSize) != steps) {
      next = std::nextafter(next, steps > 0.f
        ? std::numeric_limits<float>::infinity()
        : -std::numeric_limits<float>::infinity());

      if (std::trunc((next - appliedValue) / stepSize) != steps)
        return false;
    }

    appliedValue = next;
    logicalValue = target;
    return true;
  }
};

// ui thread only. a failed override must not fall back to a direct write.
Result applyOverride(rack::app::ParamWidget* param, float value);

// ui thread only. returns the logical encoder value, rebasing after native edits.
float getValue(rack::app::ParamWidget* param);
void forgetModule(int64_t moduleId);
void clear();

} // namespace gtnosft::param_write
