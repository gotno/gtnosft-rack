#pragma once

#include "Bundler.hpp"
#include "../OscConstants.hpp"
#include <unordered_map>
#include <vector>

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

static constexpr size_t oscPadded(size_t len) { return (len + 4) & ~3; }

// TODO: rename ModuleLightsStateBundler
struct ModuleLightsBundler : Bundler {
  typedef std::list<std::pair<rack::app::LightWidget*, LightState>> LightList;
  inline static std::unordered_map<int64_t, LightList> lights;

  ModuleLightsBundler(
    const std::vector<int64_t>& moduleIds,
    std::function<void()> callback
  );

private:
  using LightEntry = std::pair<int64_t, LightState>;

  static constexpr size_t MSG_FIXED =
    4 + // bundle element size prefix
    oscPadded(sizeof("/set/s/l") - 1) + // address string
    oscPadded(sizeof(",") - 1); // type tag base (comma prefix + null, padded)

  static constexpr size_t PER_ENTRY =
    (sizeof("hiTi") - 1) + // type tag chars per entry (h=int64, i=int32, T/F=bool, i=int32)
    sizeof(int64_t) + // moduleId
    sizeof(int32_t) + // state.id
    0 + // state.visible (bool, T/F in type tag, no data bytes)
    sizeof(int32_t); // state.color

  static constexpr size_t MAX_ENTRIES =
    (MSG_BUFFER_SIZE - EMPTY_BUNDLE_SIZE - MSG_FIXED) / PER_ENTRY;

  void collectLights(int64_t moduleId, std::vector<LightEntry>& updates);
  void addMessage(const std::vector<LightEntry>& batch);
};
