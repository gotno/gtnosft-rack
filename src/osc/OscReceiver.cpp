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

  std::string path(message.AddressPattern());
  INFO("oscrx received message on path %s", path.c_str());
  if (path.compare(std::string("/echo")) == 0) {
    try {
      osc::ReceivedMessage::const_iterator arg = message.ArgumentsBegin();

      std::string echo;
      echo = (arg++)->AsString();

      INFO("ECHO: %s", echo.c_str());

      if (arg != message.ArgumentsEnd()) throw osc::ExcessArgumentException();
    } catch(osc::Exception& e) {
      INFO("Error parsing OSC message %s: %s", message.AddressPattern(), e.what());
    }
  }
}
