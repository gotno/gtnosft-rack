#include <rack.hpp>

#include "MessagePacker.hpp"

struct ModuleInfoPacker : MessagePacker {
  ModuleInfoPacker(rack::app::ModuleWidget* _moduleWidget);
  rack::app::ModuleWidget* moduleWidget;

  float px2cm(const float& px) const;
  rack::math::Vec vec2cm(const rack::math::Vec& px) const;

  void pack(osc::OutboundPacketStream& message) override;
};
