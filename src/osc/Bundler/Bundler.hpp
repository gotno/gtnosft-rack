#pragma once

#include "rack.hpp"

#include <string>
#include <vector>
#include <functional>

#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/osc/OscTypes.h"

struct Bundler {
  Bundler(std::string _name = "NoopBundler"): name(_name) {
    // test too many messages for one packet (multiple sends)
    // for (int32_t i = 0; i < 200; ++i) {
    //   messages.emplace_back("/test", [i](osc::OutboundPacketStream& pstream) {
    //     pstream << i;
    //   });
    // }

    // test single message too large for packet (skip message)
    // messages.emplace_back("/test", [](osc::OutboundPacketStream& pstream) {
    //   for (int32_t i = 0; i < 25000; ++i) {
    //     pstream << i;
    //   }
    // });
  }
  virtual ~Bundler() {}

  std::string name{""};
  std::string getNextPath() {
    if (!hasRemainingMessages()) return "";
    return messages[messageCursor].first;
  }

  bool hasRemainingMessages() { return messageCursor < messages.size(); }
  void advance() { ++messageCursor; }

  void bundle(osc::OutboundPacketStream& pstream) {
    while(hasRemainingMessages()) {
      try {
        buildNextMessage(pstream);
      } catch (osc::OutOfBufferMemoryException& e) {
        // DEBUG(
        //   "out of memory while building %lld:%s",
        //   messageCursor,
        //   getNextPath().c_str()
        // );
        return;
      }
    }
  }

  // called before bundling to potentially skip
  std::function<bool()> noopCheck = [this]() { return messages.empty(); };
  virtual bool isNoop() { return noopCheck(); }

  // called after messages are bundled
  std::function<void()> onBundleSent = []() {};
  virtual void sent() { onBundleSent(); }

  // always called, even if bundler is noop
  std::function<void()> beforeDestroy = []() {};
  virtual void done() { beforeDestroy(); }

  // TODO: do we need this with the smaller packet sizes? doesn't seem that way.
  int32_t postSendDelayMs{0};

private:
  typedef std::function<void(osc::OutboundPacketStream&)> messageBuilder;
  typedef std::pair<std::string /* path */, messageBuilder> message;
  size_t messageCursor{0};

  void buildNextMessage(osc::OutboundPacketStream& pstream) {
    message& msg = messages[messageCursor];
    pstream << osc::BeginMessage(msg.first.c_str());
    msg.second(pstream);
    pstream << osc::EndMessage;
    advance();
  }

protected:
  std::vector<message> messages;
};
