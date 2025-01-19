#include "ModuleInfoPacker.hpp"

ModuleInfoPacker::ModuleInfoPacker(rack::app::ModuleWidget* _moduleWidget):
  moduleWidget(_moduleWidget) {
    path = "/module_info";
  }

float ModuleInfoPacker::px2cm(const float& px) const {
  return px / (rack::window::SVG_DPI / rack::window::MM_PER_IN) / 10.f;
}

rack::math::Vec ModuleInfoPacker::vec2cm(const rack::math::Vec& pxVec) const {
  return rack::math::Vec(
    px2cm(pxVec.x),
    px2cm(pxVec.y)
  );
}

void ModuleInfoPacker::pack(osc::OutboundPacketStream& message) {
  rack::math::Vec panelSize = vec2cm(moduleWidget->box.size);

  message << moduleWidget->getModule()->id
    << panelSize.x
    << panelSize.y
    ;
}
