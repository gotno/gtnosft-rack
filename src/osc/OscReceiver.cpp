#include "rack.hpp"

#include "OscReceiver.hpp"

#include "../Render.hpp"
#include "ChunkedManager.hpp"
#include "OscSender.hpp"

#include "MessagePacker/LoadedModulesPacker.hpp"

OscReceiver::OscReceiver(
  RenderWidget* _ctrl,
  OscSender* oscSender,
  ChunkedManager* chunkedManager
): ctrl(_ctrl),
  osctx(oscSender),
  chunkman(chunkedManager),
  endpoint(IpEndpointName(RX_ENDPOINT, RX_PORT)) {
    generateRoutes();
    startListener();
  }

OscReceiver::~OscReceiver() {
  endListener();
}

void OscReceiver::generateRoutes() {
  routes.emplace(
    "/ack_chunk",
    [&](osc::ReceivedMessage::const_iterator& args) {
      int32_t chunkedId = (args++)->AsInt32();
      int32_t chunkNum = (args++)->AsInt32();
      chunkman->ack(chunkedId, chunkNum);
    }
  );

  routes.emplace(
    "/get/loaded_modules",
    [this](osc::ReceivedMessage::const_iterator& args) {
      (void)args;

      ctrl->enqueueAction([this]() {
        LoadedModulesPacker* packer = new LoadedModulesPacker();
        std::vector<int64_t> moduleIds = APP->engine->getModuleIds();

        for (const auto& id : moduleIds) {
          rack::plugin::Model* model = APP->engine->getModule(id)->getModel();
          packer->addModule(id, model->plugin->slug, model->slug);
        }

        osctx->enqueueMessage(packer);
      });
    }
  );
}

void OscReceiver::startListener() {
  rxSocket = new UdpListeningReceiveSocket(endpoint, this);
  listenerThread = std::thread(&UdpListeningReceiveSocket::Run, rxSocket);
}

void OscReceiver::endListener() {
  if (rxSocket == NULL) return;

  rxSocket->AsynchronousBreak();
  listenerThread.join();
  delete rxSocket;
  rxSocket = NULL;
}

void OscReceiver::ProcessMessage(
  const osc::ReceivedMessage& message,
  const IpEndpointName& remoteEndpoint
) {
  (void)remoteEndpoint;

  // INFO("oscrx received message on path %s", message.AddressPattern());

  try {
    std::string address = message.AddressPattern();
    if (!routes.count(address)) throw osc::Exception("no route for address");

    osc::ReceivedMessage::const_iterator argsIterator = message.ArgumentsBegin();
    routes.at(address)(argsIterator);

    if (argsIterator != message.ArgumentsEnd()) throw osc::ExcessArgumentException();
  } catch(osc::Exception& e) {
    WARN("error parsing OSC message %s: %s", message.AddressPattern(), e.what());
  }
}
