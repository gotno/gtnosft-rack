#include "ParamValueAckBundler.hpp"

ParamValueAckBundler::ParamValueAckBundler(int64_t _moduleId, int32_t _paramId)
  : Bundler("ParamValueAckBundler"), moduleId(_moduleId), paramId(_paramId) {}

void ParamValueAckBundler::addMessage(float value) {
  messages.emplace_back(
    "/ack/param/value",
    [&](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << paramId
        << value
        << (value > -1);
    }
  );
}

ParamValueAckBundler* ParamValueAckBundler::success(float value) {
  addMessage(value);
  return this;
}

ParamValueAckBundler* ParamValueAckBundler::fail() {
  addMessage(-1);
  return this;
}
