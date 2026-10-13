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

  // everything except image chunks. sent ahead of queued chunks
  void enqueueBundler(Bundler* bundler);
  // image chunks. sent once lights and other queued bundlers are out
  void enqueueChunk(Bundler* bundler);
  void submitLights(Bundler* bundler);
  void drainMailboxes();
  void sendHeartbeat();

  // estimated time to send every queued chunk.
  // work time smoothing hides socket stalls, so we take the greater of:
  // queued chunks * smoothed worker time per chunk, or
  // how long the oldest queued chunk has waited
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

  // mailboxes: latest-wins single slot, processed ahead of queues
  std::atomic<Bundler*> lightsMailbox{nullptr};

  // persistent socket — rebuilt when socketDirty is set
  std::unique_ptr<UdpSocket> socket;
  std::atomic<bool> socketDirty{true};
  void rebuildSocket(SendMode mode, IpEndpointName endpoint);

  // message queue
  std::thread queueWorker;
  std::atomic<bool> queueWorkerRunning;
  std::queue<Bundler*> priorityQueue;
  struct QueuedChunk {
    Bundler* bundler;
    std::chrono::steady_clock::time_point queuedAt;
  };
  std::queue<QueuedChunk> chunkQueue;
  std::mutex qmutex;
  std::condition_variable queueLockCondition;
  std::atomic<int32_t> queuedChunks{0};
  // queuedAt of chunkQueue's front in steady_clock ns, 0 when empty.
  // written under qmutex
  std::atomic<int64_t> oldestChunkQueuedAtNs{0};
  void updateOldestChunkQueuedAt();
  // written by the queue worker only
  std::atomic<int64_t> chunkSendNs{0};
  void startQueueWorker();
  void stopQueueWorker();
  void processQueue();
};
