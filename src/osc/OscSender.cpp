#include "rack.hpp"

#include "OscSender.hpp"

#include "MessagePacker/MessagePacker.hpp"
#include "Bundler/Bundler.hpp"
#include "MessagePacker/NoopPacker.hpp"

#include "MessagePacker/BroadcastHeartbeatPacker.hpp"
#include "MessagePacker/DirectHeartbeatPacker.hpp"

#include "../util/Network.hpp"

OscSender::OscSender() {
  msgBuffer = new char[MSG_BUFFER_SIZE];

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

// TODO: makeBundle ONCE- startBundle() instead
osc::OutboundPacketStream OscSender::makeBundle() {
  osc::OutboundPacketStream bundle(msgBuffer, MSG_BUFFER_SIZE);
  bundle << osc::BeginBundleImmediate;
  return bundle;
}

osc::OutboundPacketStream OscSender::makeMessage(const std::string& address) {
  osc::OutboundPacketStream message(msgBuffer, MSG_BUFFER_SIZE);
  message << osc::BeginBundleImmediate
    << osc::BeginMessage(address.c_str());
  return message;
}

void OscSender::startBundle(osc::OutboundPacketStream& pstream) {
  pstream.Clear();
  pstream << osc::BeginBundleImmediate;
}

void OscSender::endBundle(osc::OutboundPacketStream& pstream) {
  pstream << osc::EndBundle;
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
    enqueueMessage(new BroadcastHeartbeatPacker());
  } else {
    enqueueMessage(new DirectHeartbeatPacker());
  }
}

// TODO: move sendMessage logic here
void OscSender::sendBundle(osc::OutboundPacketStream& bundle) {
  sendMessage(bundle);
}

void OscSender::sendMessage(osc::OutboundPacketStream& message) {
  try {
    UdpSocket socket;
    socket.SetEnableBroadcast(isBroadcasting());
    socket.Connect(isBroadcasting() ? broadcastEndpoint : directEndpoint);
    socket.Send(message.Data(), message.Size());
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
  // enqueueMessage(new NoopPacker());
  enqueueBundler(new Bundler());
  if (queueWorker.joinable()) queueWorker.join();
}

void OscSender::enqueueBundler(Bundler* bundler) {
  std::unique_lock<std::mutex> locker(qmutex);
  bundlerQueue.push(bundler);
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscSender::enqueueMessage(MessagePacker* packer) {
  std::unique_lock<std::mutex> locker(qmutex);
  messageQueue.push(packer);
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscSender::processQueue() {
  queueWorkerRunning = true;

  while (queueWorkerRunning) {
    std::unique_lock<std::mutex> locker(qmutex);
    queueLockCondition.wait(locker, [this](){ return !bundlerQueue.empty(); });
    // queueLockCondition.wait(locker, [this](){ return !messageQueue.empty(); });

    Bundler* bundler = bundlerQueue.front();
    bundlerQueue.pop();

    if (!bundler->isNoop()) {
      osc::OutboundPacketStream bundle = makeBundle();

      while (bundler->hasRemainingMessages()) {
        startBundle(bundle);
        bundler->bundle(bundle);
        endBundle(bundle);

        if (bundle.Size() <= EMPTY_BUNDLE_SIZE) {
          WARN(
            "bundler [%s] cannot bundle message [%s]. advancing.",
            bundler->name.c_str(),
            bundler->getNextPath().c_str()
          );
          bundler->advance();
          continue;
        }

        INFO("sending bundle");
        sendBundle(bundle);
      }

      bundler->finish();
      if (bundler->postSendDelayMs > 0)
        std::this_thread::sleep_for(
          std::chrono::milliseconds(bundler->postSendDelayMs)
        );
    }

    delete bundler;

    // MessagePacker* packer = messageQueue.front();
    // messageQueue.pop();

    // if (!packer->isNoop()) {
    //   // INFO("message queue packing message for %s", packer->path.c_str());
    //   if (packer->path.empty()) WARN("message packer has empty path");


    //   osc::OutboundPacketStream message = makeMessage(packer->path);
    //   try {
    //     packer->pack(message);
    //   } catch(osc::OutOfBufferMemoryException& e) {
    //     WARN("ModuleStructurePacker::pack() out of memory (%s)", e.what());
    //   }
    //   endMessage(message);
    //   sendMessage(message);

    //   packer->finish();

    //   if (packer->postSendDelay > 0)
    //     std::this_thread::sleep_for(std::chrono::milliseconds(packer->postSendDelay));
    // }

    // delete packer;
  }
}
