#include "Util.hpp"
#include "../bench/Bench.hpp"

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

int64_t makeRackId() {
  return rack::random::u64() % (1ull << 53);
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

  BENCH(
    static bench::Tally& createModule = bench::tally("connected_widget.create_module");
    static bench::Tally& createWidget = bench::tally("connected_widget.create_widget");
    static bench::Tally& addModule = bench::tally("connected_widget.add_module");
  )

  rack::engine::Module* module;
  rack::app::ModuleWidget* moduleWidget;
  {
    BENCH(bench::ScopedTally timer(createModule);)
    module = model->createModule();
  }
  {
    BENCH(bench::ScopedTally timer(createWidget);)
    moduleWidget = model->createModuleWidget(module);
  }
  {
    BENCH(bench::ScopedTally timer(addModule);)
    APP->engine->addModule(module);
  }
  return moduleWidget;
}

} // namespace util
} // namespace gtnosft
