#include <rack.hpp>

#include "MessagePacker.hpp"

#include <tuple>
#include <vector>

typedef std::tuple<int64_t, std::string, std::string> moduleIdentifier;

struct ModuleStubsPacker : MessagePacker {
  ModuleStubsPacker();

  std::vector<moduleIdentifier> moduleData;
  void addModule(int64_t id, std::string pluginSlug, std::string moduleSlug);

  void pack(osc::OutboundPacketStream& message) override;
};
