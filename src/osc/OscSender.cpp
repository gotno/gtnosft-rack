#include "rack.hpp"

#include "OscSender.hpp"

#include "MessagePacker/MessagePacker.hpp"
#include "MessagePacker/NoopPacker.hpp"

#include "../util/Network.hpp"

OscSender::OscSender() {
  msgBuffer = new char[MSG_BUFFER_SIZE];

  // ANY_ADDRESS by default
  directEndpoint = IpEndpointName(TX_PORT);
  broadcastEndpoint = IpEndpointName(TX_PORT);

  std::string ip, netmask;
  if (Network::get_network_info(ip, netmask)) {
    INFO("ZZZ ip: %s, netmask: %s", ip.c_str(), netmask.c_str());
  } else {
    WARN("ZZZ unable to determine ip and netmask");
  }

  std::string broadcast_ip;
  if (Network::calculate_broadcast_address(broadcast_ip)) {
    INFO("ZZZ broadcast_ip: %s", broadcast_ip.c_str());
    broadcastEndpoint = IpEndpointName(broadcast_ip.c_str(), TX_PORT);
  } else {
    WARN("ZZZ unable to determine broadcast address");
  }
  setSendMode(SendMode::Broadcast);

  startQueueWorker();
}

OscSender::~OscSender() {
  stopQueueWorker();
  delete[] msgBuffer;
}

osc::OutboundPacketStream OscSender::makeMessage(const std::string& address) {
  INFO("make message 1");
  osc::OutboundPacketStream message(msgBuffer, MSG_BUFFER_SIZE);
  INFO("make message 2");
  message << osc::BeginBundleImmediate
    << osc::BeginMessage(address.c_str());
  INFO("make message 3");
  return message;
}

void OscSender::endMessage(osc::OutboundPacketStream& message) {
  message << osc::EndMessage
    << osc::EndBundle;
}

void OscSender::setSendMode(SendMode inSendMode, std::string ip) {
  sendMode = inSendMode;

  if (sendMode == SendMode::Direct)
    directEndpoint = IpEndpointName(ip.c_str(), TX_PORT);
}

void OscSender::sendMessage(osc::OutboundPacketStream& message) {
  try {
    UdpSocket sock;

    if (sendMode == SendMode::Broadcast) {
      sock.SetEnableBroadcast(true);
      sock.Connect(broadcastEndpoint);
    } else {
      sock.Connect(directEndpoint);
    }

    sock.Send(message.Data(), message.Size());
  } catch(std::exception& e) {
    char* ip;
    if (sendMode == SendMode::Broadcast) {
      broadcastEndpoint.AddressAsString(ip);
    } else {
      directEndpoint.AddressAsString(ip);
    }

    WARN(
      "error sending OSC message to %s in %s mode: %s",
      ip,
      sendMode == SendMode::Broadcast ? "broadcast" : "direct",
      e.what()
    );
  }
}

void OscSender::startQueueWorker() {
  queueWorker = std::thread(&OscSender::processQueue, this);
}

void OscSender::stopQueueWorker() {
  queueWorkerRunning = false;
  // one last notify_one to kick it out the loop
  enqueueMessage(new NoopPacker());
  if (queueWorker.joinable()) queueWorker.join();
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
    queueLockCondition.wait(locker, [this](){ return !messageQueue.empty(); });

    MessagePacker* packer = messageQueue.front();
    messageQueue.pop();

    if (!packer->isNoop()) {
      INFO("processQueue 1");
      // INFO("message queue packing message for %s", packer->path.c_str());
      if (packer->path.empty()) WARN("message packer has empty path");
      INFO("processQueue 2");

      osc::OutboundPacketStream message = makeMessage(packer->path);
      INFO("processQueue 3");
      packer->pack(message);
      INFO("processQueue 4");
      endMessage(message);
      INFO("processQueue 5");
      sendMessage(message);
      INFO("processQueue 6");

      packer->finish();
      INFO("processQueue 7");

      if (packer->postSendDelay > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(packer->postSendDelay));
      INFO("processQueue 8");
    }

    delete packer;
    INFO("processQueue 9");
  }
}
