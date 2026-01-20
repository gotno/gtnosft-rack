#include "CablesBundler.hpp"

CablesBundler::CablesBundler(
  int64_t cableId,
  int64_t returnId
) : Bundler("CablesBundler") {
  bundleCable(cableId, returnId);
}

CablesBundler::CablesBundler(): Bundler("CablesBundler") {
  std::vector<int64_t> cableIds = APP->engine->getCableIds();

  for (int64_t cableId : cableIds) {
    bundleCable(cableId);
  }

  // TODO: send "finished" message with total sent count
}

void CablesBundler::bundleCable(int64_t cableId, int64_t returnId) {
  rack::engine::Cable* cable = APP->engine->getCable(cableId);
  rack::app::CableWidget* cableWidget = APP->scene->rack->getCable(cableId);

  int64_t inputModuleId = cable->inputModule->getId();
  int64_t outputModuleId = cable->outputModule->getId();
  int32_t inputId = cable->inputId;
  int32_t outputId = cable->outputId;
  std::string color = rack::color::toHexString(cableWidget->color);

  messages.emplace_back(
    "/set/cable",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << cableId
        << inputModuleId
        << outputModuleId
        << inputId
        << outputId
        << color.c_str()
        << returnId
        ;
    }
  );
}
