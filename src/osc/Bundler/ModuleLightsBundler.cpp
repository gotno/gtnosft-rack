#include "ModuleLightsBundler.hpp"

#include <algorithm>

ModuleLightsBundler::ModuleLightsBundler(
  const std::vector<int64_t>& subscribedModuleIds,
  std::function<void()> callback
): Bundler("ModuleLightsBundler") {
  onBundleComplete = callback;

  std::vector<int64_t> rackModuleIds = APP->engine->getModuleIds();
  std::sort(rackModuleIds.begin(), rackModuleIds.end());

  std::vector<int64_t> moduleIds(rackModuleIds.size());
  auto it = std::set_intersection(
    subscribedModuleIds.begin(),
    subscribedModuleIds.end(),
    rackModuleIds.begin(),
    rackModuleIds.end(),
    moduleIds.begin()
  );
  moduleIds.resize(it - moduleIds.begin());

  for (const auto& id : moduleIds) {
    messages.emplace_back(
      "/set/state/module/light",
      [id](osc::OutboundPacketStream& pstream) {
        INFO("bundling lights for module %lld", id);
        // pstream << id
        //   << pluginSlug.c_str()
        //   << moduleSlug.c_str()
        //   ;
      }
    );
  }
}
