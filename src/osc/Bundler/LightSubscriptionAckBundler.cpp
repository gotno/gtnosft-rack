#include "LightSubscriptionAckBundler.hpp"

LightSubscriptionAckBundler::LightSubscriptionAckBundler(
    int64_t moduleId,
    bool success
) : Bundler("LightSubscriptionAckBundler") {
  messages.emplace_back(
    "/ack/subscribe/module/lights",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << moduleId
        << success;
    }
  );
}
