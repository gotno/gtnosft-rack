#include "test.hpp"
#include "util/LightOverrides.hpp"

#include <limits>

namespace lights = gtnosft::light_overrides;

TEST(light_overrides_match_positions_with_coordinate_tolerance) {
  CHECK(lights::samePosition(39.f, 112.f, 39.f, 112.f));
  CHECK(lights::samePosition(39.0005f, 111.9995f, 39.f, 112.f));
  CHECK(!lights::samePosition(39.002f, 112.f, 39.f, 112.f));
  CHECK(!lights::samePosition(39.f, 112.002f, 39.f, 112.f));
  CHECK(!lights::samePosition(
    std::numeric_limits<float>::quiet_NaN(), 112.f, 39.f, 112.f));
}

TEST(light_overrides_dispatch_custom_functions_only_for_matching_modules) {
  bool selected = false;
  const lights::OverrideMap overrides = {
    {{"PluginA", "ModuleA"}, [&](rack::app::LightWidget*) { return selected; }},
    {{"PluginB", "ModuleB"}, [](rack::app::LightWidget*) { return true; }},
  };
  CHECK(!lights::isOverlayLight(overrides, "PluginA", "ModuleA", nullptr));
  selected = true;
  CHECK(lights::isOverlayLight(overrides, "PluginA", "ModuleA", nullptr));
  CHECK(lights::isOverlayLight(overrides, "PluginB", "ModuleB", nullptr));
  CHECK(!lights::isOverlayLight(overrides, "PluginA", "ModuleB", nullptr));
  CHECK(!lights::isOverlayLight(overrides, "Other", "ModuleA", nullptr));
  CHECK(!lights::isOverlayLight(overrides, "PluginA", "Other", nullptr));
}
