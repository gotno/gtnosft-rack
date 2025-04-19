#pragma once

#include <atomic>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/ip/UdpSocket.h"

#include "OscConstants.hpp"

class MessagePacker;
struct Bundler;
class ChunkedImage;

enum class SendMode {
  Broadcast,
  Direct
};

struct OscSender {
  OscSender();
  ~OscSender();

  void enqueueBundler(Bundler* bundler);
  void enqueueMessage(MessagePacker* packer);
  void sendHeartbeat();

  bool isBroadcasting();
  void setBroadcasting();
  void setDirect(char* ip);

private:
  char* msgBuffer;

  SendMode sendMode;

  IpEndpointName directEndpoint;
  void registerDirectEndpoint(const std::string& ip);
  IpEndpointName broadcastEndpoint;

  osc::OutboundPacketStream pstream;
  void sendBundle(osc::OutboundPacketStream& pstream);

  osc::OutboundPacketStream makeMessage(const std::string& address);
  void endMessage(osc::OutboundPacketStream& message);
  void sendMessage(osc::OutboundPacketStream& message);

  // message queue
  std::thread queueWorker;
  std::atomic<bool> queueWorkerRunning;
  std::queue<Bundler*> bundlerQueue;
  std::queue<MessagePacker*> messageQueue;
  std::mutex qmutex;
  std::condition_variable queueLockCondition;
  void startQueueWorker();
  void stopQueueWorker();
  void processQueue();
};
