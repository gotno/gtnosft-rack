#include "plugin.hpp"

#include "../../dep/oscpack/ip/UdpSocket.h"
#include "../../dep/oscpack/ip/IpEndpointName.h"
#include "../../dep/oscpack/osc/OscOutboundPacketStream.h"

#define MSG_BUFFER_SIZE (1024 * 64)
#define ENDPOINT "127.0.0.1"
#define PORT 7000

struct OscClient {
  char* msgBuffer;
  IpEndpointName endpoint;

  OscClient() {
    msgBuffer = new char[MSG_BUFFER_SIZE];
    endpoint = IpEndpointName(ENDPOINT, PORT);
  }

  ~OscClient() {
    delete[] msgBuffer;
  }

  osc::OutboundPacketStream makeMessage(std::string address) {
    osc::OutboundPacketStream message(msgBuffer, MSG_BUFFER_SIZE);
    message << osc::BeginMessage(address.c_str());
    return message;
  }

  void endMessage(osc::OutboundPacketStream& message) {
    message << osc::EndMessage;
  }

  void sendMessage(osc::OutboundPacketStream& message) {
    // this _looks like_ it only sends the data present and not the whole buffer? good.
    /* DEBUG("data: %s, size: %lld", message.Data(), message.Size()); */
    UdpTransmitSocket(endpoint).Send(message.Data(), message.Size());
  }
};
