#include "PatchInfoBundler.hpp"

#include "patch.hpp"

PatchInfoBundler::PatchInfoBundler(): Bundler("PatchInfoBundler") {
  std::string filename = rack::system::getFilename(APP->patch->path);

  int64_t ctrlId = 0;
  std::vector<int64_t> moduleIds = APP->engine->getModuleIds();
  rack::plugin::Model* model;
  for (const auto& id : moduleIds) {
    model = APP->engine->getModule(id)->getModel();
    if (model->plugin->slug == "gtnosft" && model->slug == "OSCctrl") {
      ctrlId = id;
      break;
    }
  }

  messages.emplace_back(
    "/set/patch_info",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << ctrlId
        << filename.c_str()
        ;
    }
  );
}
