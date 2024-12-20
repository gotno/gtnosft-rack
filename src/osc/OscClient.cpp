#include "plugin.hpp"
#include "OscClient.hpp"

OscClient::OscClient() {
  msgBuffer = new char[MSG_BUFFER_SIZE];
  endpoint = IpEndpointName(ENDPOINT, PORT);
  startQueueWorker();
}

OscClient::~OscClient() {
  stopQueueWorker();
  delete[] msgBuffer;
}

osc::OutboundPacketStream OscClient::makeMessage(std::string address) {
  osc::OutboundPacketStream message(msgBuffer, MSG_BUFFER_SIZE);
  message << osc::BeginMessage(address.c_str());
  return message;
}

void OscClient::endMessage(osc::OutboundPacketStream& message) {
  message << osc::EndMessage;
}

void OscClient::sendMessage(osc::OutboundPacketStream& message) {
  // this _looks like_ it only sends the data present and not the whole buffer? good.
  /* DEBUG("data: %s, size: %lld", message.Data(), message.Size()); */
  UdpTransmitSocket(endpoint).Send(message.Data(), message.Size());
}

void OscClient::startQueueWorker() {
  queueWorker = std::thread(&OscClient::processQueue, this);
}

void OscClient::stopQueueWorker() {
  queueWorkerRunning = false;
  enqueueMessage(-1); // one last notify_one to kick it out the loop
  if (queueWorker.joinable()) queueWorker.join();
}

void OscClient::enqueueMessage(int item) {
  std::unique_lock<std::mutex> locker(qmutex);
  messageQueue.push(item);
  locker.unlock();
  queueLockCondition.notify_one();
}

void OscClient::processQueue() {
  queueWorkerRunning = true;

  while (queueWorkerRunning) {
    std::unique_lock<std::mutex> locker(qmutex);
    queueLockCondition.wait(locker, [this](){ return !messageQueue.empty(); });

    int qItem = messageQueue.front();
    messageQueue.pop();

    INFO("message queue: %d", qItem);
  }
}
