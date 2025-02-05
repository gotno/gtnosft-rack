#include <thread>
#include <map>
#include <functional>

#include "../../dep/oscpack/ip/IpEndpointName.h"
#include "../../dep/oscpack/ip/UdpSocket.h"
#include "../../dep/oscpack/osc/OscPacketListener.h"
#include "../../dep/oscpack/osc/OscReceivedElements.h"

#define RX_PORT 7001

class RenderWidget;
class OscSender;
class ChunkedManager;

struct OscReceiver : public osc::OscPacketListener {
  OscReceiver(
    RenderWidget* _ctrl,
    OscSender* oscSender,
    ChunkedManager* chunkedManager
  );
  ~OscReceiver();

private:
  RenderWidget* ctrl;
  OscSender* osctx;
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

  std::map<
    std::string,
    std::function<void(osc::ReceivedMessage::const_iterator&)>
  > routes;
  void generateRoutes();
};
