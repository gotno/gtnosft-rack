#include "rack.hpp"

#include "OscSender.hpp"

#include "MessagePacker/MessagePacker.hpp"
#include "MessagePacker/NoopPacker.hpp"

OscSender::OscSender() {
  msgBuffer = new char[MSG_BUFFER_SIZE];
  endpoint = IpEndpointName(TX_ENDPOINT, TX_PORT);
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

void OscSender::sendMessage(osc::OutboundPacketStream& message) {
  UdpTransmitSocket(endpoint).Send(message.Data(), message.Size());
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
      // INFO("message queue packing message for %s", packer->path.c_str());
      if (packer->path.empty()) WARN("message packer has empty path");

      osc::OutboundPacketStream message = makeMessage(packer->path);
      packer->pack(message);
      endMessage(message);
      sendMessage(message);

      packer->finish();

      if (packer->postSendDelay > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(packer->postSendDelay));
    }

    delete packer;
  }
}
