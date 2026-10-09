#include <thread>
#include <map>
#include <functional>
#include <mutex>
#include <memory>
#include <tuple>

#include <chrono>
#include "../util/Timer.hpp"

#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/ip/UdpSocket.h"
#include "oscpack/osc/OscPacketListener.h"
#include "oscpack/osc/OscReceivedElements.h"

#include "OscConstants.hpp"
#include "../bench/Bench.hpp"

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
  void ProcessPacket(
    const char* data,
    int size,
    const IpEndpointName& remoteEndpoint
  ) override;
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

  // prevent same-frame renders of the same texture at the same size.
  // tracks /get/texture requests waiting in the action queue. newer requests
  // replace the existing request's sequence id only, and are then dropped.
  struct PendingTexture {
    int32_t sequenceId;
    BENCH(bench::TracePtr trace;)
  };
  // textureId, scale, height, width
  using PendingTextureKey = std::tuple<int64_t, float, int32_t, int32_t>;
  std::map<PendingTextureKey, std::shared_ptr<PendingTexture>> pendingTextures;
  std::mutex pendingTexturesMutex;
};
