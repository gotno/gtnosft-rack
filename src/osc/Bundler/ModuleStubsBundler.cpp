#include "ModuleStubsBundler.hpp"

ModuleStubsBundler::ModuleStubsBundler(): Bundler("ModuleStubsBundler") {}

void ModuleStubsBundler::addModule(
  int64_t id,
  std::string& pluginSlug,
  std::string& moduleSlug
) {
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
