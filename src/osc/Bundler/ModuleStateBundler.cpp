#include "ModuleStateBundler.hpp"

#include "../../util/Util.hpp"
#include "../../texture/Catalog.hpp"

ModuleStateBundler::ModuleStateBundler(int64_t moduleId, rack::math::Rect ctrlBox):
  Bundler("ModuleStateBundler") {

  rack::app::ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  if (!moduleWidget) return;

  rack::math::Vec ctrlPos =
    ctrlBox.pos.minus(rack::app::RACK_OFFSET).round();

  rack::math::Vec pos =
    moduleWidget->getPosition().minus(rack::app::RACK_OFFSET).round();
  pos = pos.minus(ctrlPos);
  pos = gtnosft::util::vec2cm(pos);

  int64_t textureId = Catalog::pullOverlayId(moduleWidget);

  messages.emplace_back(
    "/set/s/m",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << pos.x
        << pos.y
        << textureId // Overlay
        ;
    }
  );
}
