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
class SubscriptionManager;

struct OscReceiver : public osc::OscPacketListener {
  OscReceiver(
    OSCctrlWidget* _ctrl,
    OscSender* oscSender,
    ChunkedManager* chunkedManager,
    SubscriptionManager* subscriptionManager
  );
  ~OscReceiver();

  inline static int32_t activePort{RX_PORT};

private:
  OSCctrlWidget* ctrl;
  OscSender* osctx;
  ChunkedManager* chunkman;
  SubscriptionManager* subman;

  IpEndpointName endpoint;
  UdpListeningReceiveSocket* rxSocket = NULL;
	std::thread listenerThread;
  int8_t maxBindRetries{20};

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
  uint8_t missedHeartbeats{0}, maxMissedHeartbeats{MAX_MISSED_HEARTBEATS};
  std::mutex heartbeatMutex;
};
