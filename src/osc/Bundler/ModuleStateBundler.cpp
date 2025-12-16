#include "ModuleStateBundler.hpp"

#include "../../util/Util.hpp"

ModuleStateBundler::ModuleStateBundler(int64_t moduleId, rack::math::Rect ctrlBox):
  Bundler("ModuleStateBundler") {

  rack::app::ModuleWidget* moduleWidget = APP->scene->rack->getModule(moduleId);
  if (!moduleWidget) return;

  rack::math::Vec ctrlPos =
    ctrlBox.pos.minus(rack::app::RACK_OFFSET).round();

  rack::math::Vec pos =
    moduleWidget->getPosition().minus(rack::app::RACK_OFFSET).round();
  pos = pos.minus(ctrlPos);

  // if this module is on the same row and to the right of ctrl,
  // subtract ctrl's width to close the gap
  // edit: is there a better way to handle this? ideally, we'd only
  // close the gap if ctrl was in between modules
  // if (pos.y == ctrlPos.y && pos.x > 0) pos.x = pos.x - ctrlBox.size.x;

  pos = gtnosft::util::vec2cm(pos);

  messages.emplace_back(
    "/set/s/m",
    [moduleId, pos](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << pos.x
        << pos.y
        ;
    }
  );
}
