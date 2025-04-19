#include "ModuleStructureBundler.hpp"

ModuleStructureBundler::ModuleStructureBundler(
  const std::string& _pluginSlug,
  const std::string& _moduleSlug
): Bundler("ModuleStructureBundler"), pluginSlug(_pluginSlug), moduleSlug(_moduleSlug) {
  rack::plugin::Model* model = findModel();
  if (!model) {
    WARN(
      "ModuleStructureBundler unable to find Model for %s:%s",
      pluginSlug.c_str(),
      moduleSlug.c_str()
    );
    return;
  }

  rack::app::ModuleWidget* moduleWidget = model->createModuleWidget(NULL);
  panelBox = vec2cm(moduleWidget->box.size);
  delete moduleWidget;

  messages.emplace_back(
    "/set/module_structure",
    [this](osc::OutboundPacketStream& pstream) {
      pstream << pluginSlug.c_str()
        << moduleSlug.c_str()
        << panelBox.x
        << panelBox.y
        ;
    }
  );
  messages.emplace_back(
    "/stop/module_structure",
    [this](osc::OutboundPacketStream& pstream) {
      pstream << pluginSlug.c_str()
        << moduleSlug.c_str()
        ;
    }
  );
}

rack::plugin::Model* ModuleStructureBundler::findModel() {
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == pluginSlug) {
      for (rack::plugin::Model* model : plugin->models) {
        if (model->slug == moduleSlug) return model;
      }
    }
  }
  return nullptr;
}

float ModuleStructureBundler::px2cm(const float& px) const {
  return px / (rack::window::SVG_DPI / rack::window::MM_PER_IN) / 10.f;
}

rack::math::Vec ModuleStructureBundler::vec2cm(const rack::math::Vec& pxVec) const {
  return rack::math::Vec(
    px2cm(pxVec.x),
    px2cm(pxVec.y)
  );
}
