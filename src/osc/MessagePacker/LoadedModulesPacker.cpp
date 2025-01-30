#include "rack.hpp"

#include "LoadedModulesPacker.hpp"

LoadedModulesPacker::LoadedModulesPacker() {
  path = "/loaded_modules";
}

void LoadedModulesPacker::addModule(
  int64_t id,
  std::string pluginSlug,
  std::string moduleSlug
) {
  moduleData.push_back(moduleIdentifier(id, pluginSlug, moduleSlug));
}

void LoadedModulesPacker::pack(osc::OutboundPacketStream& message) {
  for(auto it = moduleData.begin(); it != moduleData.end(); it++)    {
    message << std::get<0>(*it)
      << std::get<1>(*it).c_str()
      << std::get<2>(*it).c_str()
      ;

    if (it != std::prev(moduleData.end()))
      message << osc::EndMessage
        << osc::BeginMessage(path.c_str());
        ;
  }
}
