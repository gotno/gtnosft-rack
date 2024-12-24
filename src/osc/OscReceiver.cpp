#include "plugin.hpp"
#include "OscReceiver.hpp"

OscReceiver::OscReceiver() {
  endpoint = IpEndpointName(RX_ENDPOINT, RX_PORT);
  startListener();
}

OscReceiver::~OscReceiver() {
  endListener();
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

  INFO("oscrx received message on path %s", message.AddressPattern());

  try {
    osc::ReceivedMessage::const_iterator arg = message.ArgumentsBegin();
    routeMessage(std::string(message.AddressPattern()), arg);
    if (arg != message.ArgumentsEnd()) throw osc::ExcessArgumentException();
  } catch(osc::Exception& e) {
    INFO("error parsing OSC message %s: %s", message.AddressPattern(), e.what());
  }
}

void OscReceiver::routeMessage(
  std::string path,
  osc::ReceivedMessage::const_iterator& arg
) {
  if (path == "/echo") INFO("ECHO: %s", (arg++)->AsString());
}
