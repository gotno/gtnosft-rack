#include "rack.hpp"

#include "OscSender.hpp"

#include <algorithm>

#include "../OSCctrl.hpp"
#include "Bundler/Bundler.hpp"
#include "Bundler/BroadcastHeartbeatBundler.hpp"
#include "Bundler/DirectHeartbeatBundler.hpp"

#include "../util/Network.hpp"
#include "../bench/Bench.hpp"

OscSender::OscSender(OSCctrlWidget* _ctrl): ctrl(_ctrl),
  msgBuffer(new char[MSG_BUFFER_SIZE]),
  pstream(osc::OutboundPacketStream(msgBuffer, MSG_BUFFER_SIZE)) {

  std::string broadcast_ip;
  if (Network::calculate_broadcast_address(broadcast_ip)) {
    broadcastEndpoint = IpEndpointName(broadcast_ip.c_str(), TX_PORT);
  } else {
    WARN("OSCctrl unable to determine network broadcast address!");
  }
  setBroadcasting();
  startQueueWorker();
}

OscSender::~OscSender() {
  stopQueueWorker();

  // free anything the worker didn't get to
  drainMailboxes();
  while (!priorityQueue.empty()) {
    Bundler* bundler = priorityQueue.front();
    priorityQueue.pop();
    bundler->done();
    delete bundler;
  }
  while (!chunkQueue.empty()) {
    Bundler* bundler = chunkQueue.front().bundler;
    chunkQueue.pop();
    bundler->done();
    delete bundler;
  }

  delete[] msgBuffer;
}

osc::OutboundPacketStream OscSender::makeMessage(const std::string& address) {
  osc::OutboundPacketStream message(msgBuffer, MSG_BUFFER_SIZE);
  message << osc::BeginBundleImmediate
    << osc::BeginMessage(address.c_str());
  return message;
}

void OscSender::setBroadcasting() {
  OSCctrl* module = dynamic_cast<OSCctrl*>(ctrl->module);
  module->broadcasting = true;

  std::unique_lock<std::mutex> locker(qmutex);
  sendMode = SendMode::Broadcast;
  socketDirty.store(true);
}

bool OscSender::isBroadcasting() {
  return sendMode == SendMode::Broadcast;
}

void OscSender::setDirect(char* ip) {
  OSCctrl* module = dynamic_cast<OSCctrl*>(ctrl->module);
  module->broadcasting = false;

  std::unique_lock<std::mutex> locker(qmutex);
  sendMode = SendMode::Direct;
  directEndpoint = IpEndpointName(ip, TX_PORT);
  socketDirty.store(true);
}

void OscSender::sendHeartbeat() {
  OSCctrl* module = dynamic_cast<OSCctrl*>(ctrl->module);

  // TODO: immediate via deque
  if (isBroadcasting()) {
    module->txPulse.trigger();
    enqueueBundler(new BroadcastHeartbeatBundler(ctrl->module->id));
  } else {
    module->hbOutPulse.trigger();
    enqueueBundler(new DirectHeartbeatBundler());
  }
}

void OscSender::rebuildSocket(SendMode mode, IpEndpointName endpoint) {
  try {
    socket = std::make_unique<UdpSocket>();
    socket->SetEnableBroadcast(mode == SendMode::Broadcast);
    socket->Connect(endpoint);
  } catch(std::exception& e) {
    WARN("error creating OSC socket: %s", e.what());
    socket.reset();
    socketDirty.store(true);
  }
}

void OscSender::sendBundle(osc::OutboundPacketStream& pstream) {
  if (!socket) return;
  BENCH(
    static bench::Tally& txPackets = bench::tally("tx.packets");
    bench::ScopedTally txTimer(txPackets);
  )
  try {
    socket->Send(pstream.Data(), pstream.Size());
  } catch(std::exception& e) {
    char* ip = (char*)malloc(IpEndpointName::ADDRESS_STRING_LENGTH + 1);

    if (isBroadcasting()) {
      broadcastEndpoint.AddressAsString(ip);
    } else {
      directEndpoint.AddressAsString(ip);
    }

    WARN(
      "error sending OSC message to %s in %s mode: %s",
      ip,
      isBroadcasting() ? "broadcast" : "direct",
      e.what()
    );

    free(ip);
    socketDirty.store(true);
  }
}

void OscSender::startQueueWorker() {
  queueWorkerRunning = true;
  queueWorker = std::thread(&OscSender::processQueue, this);
}

void OscSender::stopQueueWorker() {
  queueWorkerRunning = false;
  // one last notify_one to kick it out the loop
  enqueueBundler(new Bundler());
  if (queueWorker.joinable()) queueWorker.join();
}

void OscSender::enqueueBundler(Bundler* bundler) {
  if (!isBroadcasting()) {
    OSCctrl* module = dynamic_cast<OSCctrl*>(ctrl->module);
    module->txPulse.trigger();
  }

  std::unique_lock<std::mutex> locker(qmutex);
  priorityQueue.push(bundler);
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscSender::enqueueChunk(Bundler* bundler) {
  if (!isBroadcasting()) {
    OSCctrl* module = dynamic_cast<OSCctrl*>(ctrl->module);
    module->txPulse.trigger();
  }

  std::unique_lock<std::mutex> locker(qmutex);
  chunkQueue.push({bundler, std::chrono::steady_clock::now()});
  queuedChunks.fetch_add(1, std::memory_order_relaxed);
  if (chunkQueue.size() == 1) updateOldestChunkQueuedAt();
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscSender::submitLights(Bundler* bundler) {
  Bundler* old = lightsMailbox.exchange(bundler);
  if (old) {
    old->done();
    delete old;
    return;
  }

  // mailbox was empty, kick worker
  std::unique_lock<std::mutex> locker(qmutex);
  queueLockCondition.notify_one();
}

void OscSender::drainMailboxes() {
  if (Bundler* old = lightsMailbox.exchange(nullptr)) {
    old->done();
    delete old;
  }
}

void OscSender::processQueue() {
  while (queueWorkerRunning) {
    std::unique_lock<std::mutex> locker(qmutex);
    queueLockCondition.wait(locker, [this](){
      return !priorityQueue.empty()
        || !chunkQueue.empty()
        || lightsMailbox.load() != nullptr;
    });

    // lights mailbox first, then everything else, then image chunks
    Bundler* bundler = lightsMailbox.exchange(nullptr);
    bool isChunk = false;
    if (!bundler && !priorityQueue.empty()) {
      bundler = priorityQueue.front();
      priorityQueue.pop();
    }
    if (!bundler && !chunkQueue.empty()) {
      bundler = chunkQueue.front().bundler;
      chunkQueue.pop();
      queuedChunks.fetch_sub(1, std::memory_order_relaxed);
      updateOldestChunkQueuedAt();
      isChunk = true;
    }

    bool dirty = socketDirty.exchange(false);
    IpEndpointName endpoint =
      (sendMode == SendMode::Broadcast) ? broadcastEndpoint : directEndpoint;

    locker.unlock();

    if (dirty || !socket) rebuildSocket(sendMode, endpoint);

    if (!bundler) return;

    auto sendStart = std::chrono::steady_clock::now();

    if (!bundler->isNoop()) {
      while (bundler->hasRemainingMessages()) {
        pstream.Clear();
        pstream << osc::BeginBundleImmediate;

        bundler->bundle(pstream);

        pstream << osc::EndBundle;

        if (pstream.Size() <= EMPTY_BUNDLE_SIZE) {
          WARN(
            "bundler [%s] cannot bundle message [%s]. advancing.",
            bundler->name.c_str(),
            bundler->getNextPath().c_str()
          );
          bundler->advance();
          continue;
        }

        sendBundle(pstream);
      }

      bundler->sent();
      if (bundler->postSendDelayMs > 0)
        std::this_thread::sleep_for(
          std::chrono::milliseconds(bundler->postSendDelayMs)
        );
    }

    bundler->done();
    delete bundler;

    if (!isChunk) continue;
    int64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - sendStart
    ).count();
    int64_t smoothed = chunkSendNs.load(std::memory_order_relaxed);
    smoothed = smoothed == 0 ? elapsed : smoothed + (elapsed - smoothed) / 8;
    chunkSendNs.store(smoothed, std::memory_order_relaxed);
  }
}

void OscSender::updateOldestChunkQueuedAt() {
  oldestChunkQueuedAtNs.store(
    chunkQueue.empty()
      ? 0
      : std::chrono::duration_cast<std::chrono::nanoseconds>(
          chunkQueue.front().queuedAt.time_since_epoch()
        ).count(),
    std::memory_order_relaxed
  );
}

std::chrono::nanoseconds OscSender::backlog() const {
  std::chrono::nanoseconds estimate(
    queuedChunks.load(std::memory_order_relaxed)
      * chunkSendNs.load(std::memory_order_relaxed)
  );
  int64_t oldestAt = oldestChunkQueuedAtNs.load(std::memory_order_relaxed);
  if (oldestAt == 0) return estimate;
  std::chrono::nanoseconds age =
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()
    ) - std::chrono::nanoseconds(oldestAt);
  return std::max(estimate, age);
}
