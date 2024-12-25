#include "plugin.hpp"
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
  message << osc::BeginMessage(address.c_str());
  return message;
}

void OscSender::endMessage(osc::OutboundPacketStream& message) {
  message << osc::EndMessage;
}

void OscSender::sendMessage(osc::OutboundPacketStream& message) {
  // this _looks like_ it only sends the data present and not the whole buffer? good.
  /* DEBUG("data: %s, size: %lld", message.Data(), message.Size()); */
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

    INFO("message queue packing message for %s", packer->path.c_str());

    if (!packer->isNoop()) {
      osc::OutboundPacketStream message = makeMessage(packer->path);
      packer->pack(message);
      endMessage(message);
      sendMessage(message);
    }

    delete packer;
    packer = NULL;
  }
}
