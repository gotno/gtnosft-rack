#include "ModuleStructurePacker.hpp"

#include <plugin.hpp>

ModuleStructurePacker::ModuleStructurePacker(
  const std::string& _pluginSlug,
  const std::string& _moduleSlug
): pluginSlug(_pluginSlug), moduleSlug(_moduleSlug), panelBox(-1.f, -1.f) {
  path = "/set/module_structure";

  rack::plugin::Model* model = findModel();
  if (!model) {
    WARN(
      "ModuleStructurePacker unable to find Model for %s:%s",
      pluginSlug.c_str(),
      moduleSlug.c_str()
    );
    return;
  }

  rack::app::ModuleWidget* moduleWidget = model->createModuleWidget(NULL);
  panelBox = vec2cm(moduleWidget->box.size);
  delete moduleWidget;
}

rack::plugin::Model* ModuleStructurePacker::findModel() {
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == pluginSlug) {
      for (rack::plugin::Model* model : plugin->models) {
        if (model->slug == moduleSlug) return model;
      }
    }
  }
  return nullptr;
}

float ModuleStructurePacker::px2cm(const float& px) const {
  return px / (rack::window::SVG_DPI / rack::window::MM_PER_IN) / 10.f;
}

rack::math::Vec ModuleStructurePacker::vec2cm(const rack::math::Vec& pxVec) const {
  return rack::math::Vec(
    px2cm(pxVec.x),
    px2cm(pxVec.y)
  );
}

void ModuleStructurePacker::pack(osc::OutboundPacketStream& message) {
  message << pluginSlug.c_str()
    << moduleSlug.c_str()
    << panelBox.x
    << panelBox.y
    ;
}
