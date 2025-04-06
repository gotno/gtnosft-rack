#include <rack.hpp>

#include "MessagePacker.hpp"

struct ModuleStructurePacker : MessagePacker {
  ModuleStructurePacker(
    const std::string& pluginSlug,
    const std::string& moduleSlug
  );

  std::string pluginSlug, moduleSlug;
  rack::math::Vec panelBox;

  rack::plugin::Model* findModel();

  float px2cm(const float& px) const;
  rack::math::Vec vec2cm(const rack::math::Vec& px) const;


  void pack(osc::OutboundPacketStream& message) override;
};
