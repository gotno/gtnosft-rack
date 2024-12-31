#include <thread>

#include "../../dep/oscpack/ip/IpEndpointName.h"
#include "../../dep/oscpack/ip/UdpSocket.h"
#include "../../dep/oscpack/osc/OscPacketListener.h"
#include "../../dep/oscpack/osc/OscReceivedElements.h"

#define RX_ENDPOINT "127.0.0.1"
#define RX_PORT 7001

class ChunkedManager;

struct OscReceiver : public osc::OscPacketListener {
  OscReceiver(ChunkedManager* chunkedManager);
  ~OscReceiver();

private:
  ChunkedManager* chunkman;

  IpEndpointName endpoint;
  UdpListeningReceiveSocket* rxSocket = NULL;
	std::thread listenerThread;

  void startListener();
  void endListener();
  void ProcessMessage(
    const osc::ReceivedMessage& message,
    const IpEndpointName& remoteEndpoint
  ) override;
  void routeMessage(
    std::string path,
    osc::ReceivedMessage::const_iterator& arg
  );
};
