#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "oscpack/ip/IpEndpointName.h"
#include "oscpack/osc/OscOutboundPacketStream.h"
#include "oscpack/ip/UdpSocket.h"

#include "OscConstants.hpp"

class OSCctrlWidget;
struct Bundler;
class ChunkedImage;

enum class SendMode {
  Broadcast,
  Direct
};

struct OscSender {
  OscSender(OSCctrlWidget* _ctrl);
  ~OscSender();

  OSCctrlWidget* ctrl;

  void enqueueBundler(Bundler* bundler);
  void submitLights(Bundler* bundler);
  void drainMailboxes();
  void sendHeartbeat();

  // estimated time to send everything queued.
  // work time smoothing hides socket stalls, so we take the greater of:
  // queued bundlers * smoothed worker time, or
  // how long the oldest queued bundler has waited
  std::chrono::nanoseconds backlog() const;

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

  // mailboxes: latest-wins single slot, processed ahead of rest of queue
  std::atomic<Bundler*> lightsMailbox{nullptr};

  // persistent socket — rebuilt when socketDirty is set
  std::unique_ptr<UdpSocket> socket;
  std::atomic<bool> socketDirty{true};
  void rebuildSocket(SendMode mode, IpEndpointName endpoint);

  // message queue
  std::thread queueWorker;
  std::atomic<bool> queueWorkerRunning;
  struct QueuedBundler {
    Bundler* bundler;
    std::chrono::steady_clock::time_point queuedAt;
  };
  std::queue<QueuedBundler> bundlerQueue;
  std::mutex qmutex;
  std::condition_variable queueLockCondition;
  std::atomic<int32_t> queuedBundlers{0};
  // queuedAt of the queue's front in steady_clock ns, 0 when empty.
  // written under qmutex
  std::atomic<int64_t> oldestQueuedAtNs{0};
  void updateOldestQueuedAt();
  // written by the queue worker only
  std::atomic<int64_t> bundlerSendNs{0};
  void startQueueWorker();
  void stopQueueWorker();
  void processQueue();
};
