#include <thread>
#include <map>
#include <functional>
#include <mutex>

#include <chrono>
#include "../util/Timer.hpp"

#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/ip/UdpSocket.h"
#include "oscpack/osc/OscPacketListener.h"
#include "oscpack/osc/OscReceivedElements.h"

#include "OscConstants.hpp"

class OSCctrlWidget;
class OscSender;
class ChunkedManager;

struct OscReceiver : public osc::OscPacketListener {
  OscReceiver(
    OSCctrlWidget* _ctrl,
    OscSender* oscSender,
    ChunkedManager* chunkedManager
  );
  ~OscReceiver();

private:
  OSCctrlWidget* ctrl;
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
    std::function<
      void(
        osc::ReceivedMessage::const_iterator&,
        const IpEndpointName&
      )
    >
  > routes;
  void generateRoutes();

  void startHeartbeat();
  std::chrono::time_point<std::chrono::steady_clock> lastHeartbeatRxTime =
    std::chrono::steady_clock::time_point::min();
  Interval heartbeatInterval;
  uint32_t heartbeatIntervalDelay{HEARTBEAT_DELAY};
  uint8_t missedHeartbeats{0}, maxMissedHeartbeats{5};
  std::mutex heartbeatMutex;
};
