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
#include "ChunkPacer.hpp"
#include <string>
#include "../bench/Bench.hpp"

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
  // safe from any thread with a Rack context set. goes through its own
  // mailbox, ahead of everything else, so a backed-up queue can't delay it.
  // firedAt: when the heartbeat timer fired (bench builds only)
  void sendHeartbeat(BENCH(bench::time_point firedAt = {}));

  // estimated time to send every queued chunk.
  // work time smoothing hides socket stalls, so we take the greater of:
  // queued chunks * smoothed worker time per chunk, or
  // how long the oldest queued chunk has waited
  std::chrono::nanoseconds backlog() const;

  bool isBroadcasting();
  void setBroadcasting();
  void setDirect(char* ip);

  // paces image chunks while an audio stream plays
  ChunkPacer pacer;
  // the registered client's address
  std::string directHost();

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
  std::atomic<Bundler*> heartbeatMailbox{nullptr};
  // swap a bundler into a mailbox, freeing the one it replaces
  void post(std::atomic<Bundler*>& mailbox, Bundler* bundler);

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

  BENCH(
    // queue worker only
    bench::time_point lastHeartbeatSentAt{};
    uint64_t lastHeartbeatGeneration{0};
    void recordSent(
      Bundler* bundler,
      bench::time_point sendStart,
      bool fromPriority
    );
  )
};
