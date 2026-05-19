#include "CableAckBundler.hpp"

CableAckBundler::CableAckBundler(
  CableAckType _type,
  int64_t _returnId
) : Bundler("CableAckBundler"), type(_type), returnId(_returnId) {
  assert(type != CableAckType::Unknown);
  address = type == CableAckType::Add ? "/ack/cable/add" : "/ack/cable/remove";
}

CableAckBundler* CableAckBundler::success(int64_t cableId) {
  assert(type != CableAckType::Unknown);

  if (type == CableAckType::Add) {
    messages.emplace_back(
      address.c_str(),
      [&](osc::OutboundPacketStream& pstream) {
        pstream << returnId
          << cableId
          << true;
      }
    );
  }
  if (type == CableAckType::Remove) {
    messages.emplace_back(
      address.c_str(),
      [&](osc::OutboundPacketStream& pstream) {
        pstream << cableId
          << true;
      }
    );
  }

  return this;
}

CableAckBundler* CableAckBundler::fail(int64_t cableId) {
  assert(type != CableAckType::Unknown);

  if (type == CableAckType::Add) {
    messages.emplace_back(
      address.c_str(),
      [&](osc::OutboundPacketStream& pstream) {
        pstream << returnId
          << cableId
          << false;
      }
    );
  }
  if (type == CableAckType::Remove) {
    messages.emplace_back(
      address.c_str(),
      [&](osc::OutboundPacketStream& pstream) {
        pstream << cableId
          << false;
      }
    );
  }

  return this;
}
