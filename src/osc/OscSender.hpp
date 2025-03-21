#pragma once

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/ip/UdpSocket.h"

#define TX_PORT 7746 // RSIM

class MessagePacker;
class ChunkedImage;

enum class SendMode {
  Broadcast,
  Direct
};

struct OscSender {
  static const int32_t MSG_BUFFER_SIZE = 1280; // safely under 1500 octect MTU?

  OscSender();
  ~OscSender();

  void enqueueMessage(MessagePacker* packer);

private:
  char* msgBuffer;

  IpEndpointName directEndpoint;
  void registerDirectEndpoint(const std::string& ip);
  IpEndpointName broadcastEndpoint;

  osc::OutboundPacketStream makeMessage(const std::string& address);
  void endMessage(osc::OutboundPacketStream& message);
  void sendMessage(osc::OutboundPacketStream& message);

  void setSendMode(SendMode inSendMode, std::string ip = "");
  SendMode sendMode;

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
