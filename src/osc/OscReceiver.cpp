#include "plugin.hpp"

#include "OscReceiver.hpp"

#include "ChunkedManager.hpp"

OscReceiver::OscReceiver(ChunkedManager* chunkedManager):
  chunkman(chunkedManager), endpoint(IpEndpointName(RX_ENDPOINT, RX_PORT)) {
    generateRoutes();
    startListener();
  }

OscReceiver::~OscReceiver() {
  endListener();
}

void OscReceiver::generateRoutes() {
  routes.emplace("/ack_chunk", [&](osc::ReceivedMessage::const_iterator& args) {
    int32_t chunkedId, chunkNum;
    chunkedId = (args++)->AsInt32();
    chunkNum = (args++)->AsInt32();
    chunkman->ack(chunkedId, chunkNum);
  });
}

void OscReceiver::startListener() {
  rxSocket = new UdpListeningReceiveSocket(endpoint, this);
  listenerThread = std::thread(&UdpListeningReceiveSocket::Run, rxSocket);
}

void OscReceiver::endListener() {
  if (rxSocket == NULL) return;

  rxSocket->AsynchronousBreak();
  listenerThread.join();
  delete rxSocket;
  rxSocket = NULL;
}

void OscReceiver::ProcessMessage(
  const osc::ReceivedMessage& message,
  const IpEndpointName& remoteEndpoint
) {
  (void)remoteEndpoint;

  // INFO("oscrx received message on path %s", message.AddressPattern());

  try {
    std::string address = message.AddressPattern();
    if (!routes.count(address))  throw osc::Exception("no route for address");

    osc::ReceivedMessage::const_iterator argsIterator = message.ArgumentsBegin();
    routes.at(address)(argsIterator);

    if (argsIterator != message.ArgumentsEnd()) throw osc::ExcessArgumentException();
  } catch(osc::Exception& e) {
    WARN("error parsing OSC message %s: %s", message.AddressPattern(), e.what());
  }
}
