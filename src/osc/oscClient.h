#include "plugin.hpp"

#include "oscpack/ip/UdpSocket.h"
#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/osc/OscOutboundPacketStream.h"

#include <thread>
#include <queue>

#define MSG_BUFFER_SIZE 65507 // oscpack MAX_BUFFER_SIZE
#define ENDPOINT "127.0.0.1"
#define PORT 7000

struct OscClient {
  uint8_t* msgBuffer;
  IpEndpointName endpoint;

  // message queue
  /* std::thread queueWorker; */
  /* std::atomic<bool> queueWorkerRunning; */
  /* std::queue<Command> commandQueue; */
  /* std::mutex qmutex; */
  /* std::condition_variable queueLockCondition; */
  /* float_time_point getCurrentTime(); */

  /* void enqueueCommand(Command command); */
  /* void startQueueWorker(); */
  /* void processQueue(); */
  /* void stopQueueWorker(); */


  OscClient() {
    msgBuffer = new uint8_t[MSG_BUFFER_SIZE];
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
