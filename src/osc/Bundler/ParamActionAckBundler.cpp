#include "ParamActionAckBundler.hpp"

ParamActionAckBundler::ParamActionAckBundler(
  int64_t _moduleId,
  int32_t _paramId,
  const std::string& _action
) : Bundler("ParamActionAckBundler"),
  moduleId(_moduleId), paramId(_paramId), action(_action) {}

void ParamActionAckBundler::addMessage(bool success) {
  messages.emplace_back(
    "/ack/" + action + "/param",
    [this, success](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << paramId
        << success
        ;
    }
  );
}

ParamActionAckBundler* ParamActionAckBundler::success() {
  addMessage(true);
  return this;
}

ParamActionAckBundler* ParamActionAckBundler::fail() {
  addMessage(false);
  return this;
}
