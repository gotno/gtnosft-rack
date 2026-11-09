#include "LightOverrides.hpp"

#include <rack.hpp>

namespace gtnosft::light_overrides {

const OverrideMap overlayLights = {
  {
    {"Befaco", "NoisePlethora"},
    [](rack::app::LightWidget* light) {
      for (float yPos : {38.172f, 50.712f}) {
        auto position = rack::window::mm2px(rack::math::Vec(13.106f, yPos));
        if (samePosition(light->box.pos.x, light->box.pos.y, position.x, position.y))
          return true;
      }
      return false;
    }
  },
  {
    {"Befaco", "Iroi"},
    [](rack::app::LightWidget* light) {
      auto* moduleLight = dynamic_cast<rack::app::ModuleLightWidget*>(light);
      if (!moduleLight) return false;

      // Iroi's filter type, filter position, and modulation glyph ids are contiguous
      constexpr int filterTypeLp = 13;
      constexpr int modulationTypeEnvf = 27;
      return moduleLight->firstLightId >= filterTypeLp &&
        moduleLight->firstLightId <= modulationTypeEnvf;
    }
  },
};

bool isOverlayLight(rack::app::ModuleWidget* module, rack::app::LightWidget* light) {
  auto* model = module->model;
  if (!model || !model->plugin) return false;

  return isOverlayLight(overlayLights, model->plugin->slug, model->slug, light);
}

} // namespace gtnosft::light_overrides
