#pragma once

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/ip/UdpSocket.h"

#define TX_ENDPOINT "127.0.0.1"
#define TX_PORT 4924

class MessagePacker;
class ChunkedImage;

struct OscSender {
  static const int32_t MSG_BUFFER_SIZE = 65507; // oscpack MAX_BUFFER_SIZE

  OscSender();
  ~OscSender();

  void enqueueMessage(MessagePacker* packer);

private:
  char* msgBuffer;
  IpEndpointName endpoint;
  osc::OutboundPacketStream makeMessage(const std::string& address);
  void endMessage(osc::OutboundPacketStream& message);
  void sendMessage(osc::OutboundPacketStream& message);

  // message queue
  std::thread queueWorker;
  std::atomic<bool> queueWorkerRunning;
  std::queue<MessagePacker*> messageQueue;
  std::mutex qmutex;
  std::condition_variable queueLockCondition;
  void startQueueWorker();
  void stopQueueWorker();
  void processQueue();
};
