#include "ParamAckBundler.hpp"

ParamAckBundler::ParamAckBundler(int64_t _moduleId, int32_t _paramId)
  : Bundler("ParamAckBundler"), moduleId(_moduleId), paramId(_paramId) {}

ParamAckBundler* ParamAckBundler::success(float value) {
  messages.emplace_back(
    "/ack/param/value",
    [&](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << paramId
        << value
        << true;
    }
  );

  return this;
}

ParamAckBundler* ParamAckBundler::fail() {
  messages.emplace_back(
    "/ack/param/value",
    [&](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << paramId
        << -1
        << false;
    }
  );

  return this;
}
