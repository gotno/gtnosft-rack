#include "Util.hpp"

namespace gtnosft {
namespace util {

float px2cm(const float& px) {
  return px / (rack::window::SVG_DPI / rack::window::MM_PER_IN) / 10.f;
}

rack::math::Vec vec2cm(const rack::math::Vec& pxVec) {
  return rack::math::Vec(
    px2cm(pxVec.x),
    px2cm(pxVec.y)
  );
}

rack::plugin::Model* findModel(std::string pluginSlug, std::string moduleSlug) {
  for (rack::plugin::Plugin* plugin : rack::plugin::plugins) {
    if (plugin->slug == pluginSlug) {
      for (rack::plugin::Model* model : plugin->models) {
        if (model->slug == moduleSlug) return model;
      }
    }
  }

  WARN("unable to find Model for %s:%s", pluginSlug.c_str(), moduleSlug.c_str());
  return NULL;
}

rack::app::ModuleWidget* makeModuleWidget(rack::plugin::Model* model) {
  if (!model) return NULL;
  return model->createModuleWidget(NULL);
}

rack::app::ModuleWidget* makeConnectedModuleWidget(rack::plugin::Model* model) {
  if (!model) return NULL;

  rack::engine::Module* module = model->createModule();
  rack::app::ModuleWidget* moduleWidget = model->createModuleWidget(module);
  APP->engine->addModule(module);
  return moduleWidget;
}

} // namespace util
} // namespace gtnosft
