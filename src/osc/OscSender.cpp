#include "plugin.hpp"
#include "OscSender.hpp"

OscSender::OscSender() {
  msgBuffer = new char[MSG_BUFFER_SIZE];
  endpoint = IpEndpointName(TX_ENDPOINT, TX_PORT);
  startQueueWorker();
}

OscSender::~OscSender() {
  stopQueueWorker();
  delete[] msgBuffer;
}

osc::OutboundPacketStream OscSender::makeMessage(std::string address) {
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
  enqueueMessage(-1); // one last notify_one to kick it out the loop
  if (queueWorker.joinable()) queueWorker.join();
}

void OscSender::enqueueMessage(int item) {
  std::unique_lock<std::mutex> locker(qmutex);
  messageQueue.push(item);
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscSender::processQueue() {
  queueWorkerRunning = true;

  while (queueWorkerRunning) {
    std::unique_lock<std::mutex> locker(qmutex);
    queueLockCondition.wait(locker, [this](){ return !messageQueue.empty(); });

    int qItem = messageQueue.front();
    messageQueue.pop();

    INFO("message queue: %d", qItem);
  }
}
