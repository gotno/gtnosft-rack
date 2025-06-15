#include "rack.hpp"

#include "OscSender.hpp"

#include "Bundler/Bundler.hpp"
#include "Bundler/BroadcastHeartbeatBundler.hpp"
#include "Bundler/DirectHeartbeatBundler.hpp"

#include "../util/Network.hpp"

OscSender::OscSender():
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
  delete[] msgBuffer;
}

osc::OutboundPacketStream OscSender::makeMessage(const std::string& address) {
  osc::OutboundPacketStream message(msgBuffer, MSG_BUFFER_SIZE);
  message << osc::BeginBundleImmediate
    << osc::BeginMessage(address.c_str());
  return message;
}

void OscSender::endMessage(osc::OutboundPacketStream& message) {
  message << osc::EndMessage
    << osc::EndBundle;
}

void OscSender::setBroadcasting() {
  sendMode = SendMode::Broadcast;
}

bool OscSender::isBroadcasting() {
  return sendMode == SendMode::Broadcast;
}

void OscSender::setDirect(char* ip) {
  sendMode = SendMode::Direct;
  directEndpoint = IpEndpointName(ip, TX_PORT);
}

void OscSender::sendHeartbeat() {
  // TODO: immediate via deque
  if (isBroadcasting()) {
    enqueueBundler(new BroadcastHeartbeatBundler());
  } else {
    enqueueBundler(new DirectHeartbeatBundler());
  }
}

void OscSender::sendBundle(osc::OutboundPacketStream& pstream) {
  try {
    UdpSocket socket;
    socket.SetEnableBroadcast(isBroadcasting());
    socket.Connect(isBroadcasting() ? broadcastEndpoint : directEndpoint);
    socket.Send(pstream.Data(), pstream.Size());
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
  }
}

void OscSender::startQueueWorker() {
  queueWorker = std::thread(&OscSender::processQueue, this);
}

void OscSender::stopQueueWorker() {
  queueWorkerRunning = false;
  // one last notify_one to kick it out the loop
  enqueueBundler(new Bundler());
  if (queueWorker.joinable()) queueWorker.join();
}

void OscSender::enqueueBundler(Bundler* bundler) {
  std::unique_lock<std::mutex> locker(qmutex);
  bundlerQueue.push(bundler);
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscSender::processQueue() {
  queueWorkerRunning = true;

  while (queueWorkerRunning) {
    std::unique_lock<std::mutex> locker(qmutex);
    queueLockCondition.wait(locker, [this](){ return !bundlerQueue.empty(); });

    Bundler* bundler = bundlerQueue.front();
    bundlerQueue.pop();

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
  }
}
