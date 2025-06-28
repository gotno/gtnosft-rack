#include "ModuleStubsBundler.hpp"

ModuleStubsBundler::ModuleStubsBundler(): Bundler("ModuleStubsBundler") {
  std::vector<int64_t> moduleIds = APP->engine->getModuleIds();

  rack::plugin::Model* model;
  for (const auto& id : moduleIds) {
    model = APP->engine->getModule(id)->getModel();
    std::string pluginSlug = model->plugin->slug;
    std::string moduleSlug = model->slug;

    messages.emplace_back(
      "/set/module_stub",
      [id, pluginSlug, moduleSlug](osc::OutboundPacketStream& pstream) {
        pstream << id
          << pluginSlug.c_str()
          << moduleSlug.c_str()
          ;
      }
    );
  }
}
