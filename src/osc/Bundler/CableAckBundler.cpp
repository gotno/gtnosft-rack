#include "CableAckBundler.hpp"

CableAckBundler::CableAckBundler(
  CableAckType _type,
  int64_t _returnId
) : Bundler("CableAckBundler"), type(_type), returnId(_returnId) {
  assert(type != CableAckType::Unknown);
}

CableAckBundler* CableAckBundler::success(int64_t cableId) {
  if (type == CableAckType::Add) {
    messages.emplace_back(
      "/ack/cable/add",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << returnId
          << cableId
          << true;
      }
    );
  } else {
    messages.emplace_back(
      "/ack/cable/remove",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << cableId
          << true;
      }
    );
  }

  return this;
}

CableAckBundler* CableAckBundler::fail(int64_t cableId) {
  if (type == CableAckType::Add) {
    messages.emplace_back(
      "/ack/cable/add",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << returnId
          << cableId
          << false;
      }
    );
  } else {
    messages.emplace_back(
      "/ack/cable/remove",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << cableId
          << false;
      }
    );
  }

  return this;
}
