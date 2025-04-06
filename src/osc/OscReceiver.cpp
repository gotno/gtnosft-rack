#include "rack.hpp"
#include "patch.hpp"

#include "OscReceiver.hpp"

#include "../OSCctrl.hpp"
#include "ChunkedManager.hpp"
#include "OscSender.hpp"

#include "MessagePacker/ModuleStubsPacker.hpp"
#include "MessagePacker/ModuleStructurePacker.hpp"

OscReceiver::OscReceiver(
  OSCctrlWidget* _ctrl,
  OscSender* oscSender,
  ChunkedManager* chunkedManager
): ctrl(_ctrl),
  osctx(oscSender),
  chunkman(chunkedManager),
  endpoint(IpEndpointName(IpEndpointName::ANY_ADDRESS, RX_PORT)) {
    generateRoutes();
    startListener();
    startHeartbeat();
  }

OscReceiver::~OscReceiver() {
  endListener();
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

void OscReceiver::startHeartbeat() {
  heartbeatInterval = Timer::setInterval(heartbeatIntervalDelay, [this] {
    ctrl->enqueueAction([this]() {
      osctx->sendHeartbeat();
    });

    std::scoped_lock<std::mutex> lock(heartbeatMutex);

    // client isn't sending heartbeats or hasn't started yet
    if (lastHeartbeatRxTime == std::chrono::steady_clock::time_point::min())
      return;

    auto now = std::chrono::steady_clock::now();
    auto delayMs = std::chrono::milliseconds(heartbeatIntervalDelay);

    if (lastHeartbeatRxTime < (now - delayMs)) ++missedHeartbeats;

    // client is gone, switch back to broadcasting
    if (missedHeartbeats >= maxMissedHeartbeats) {
      lastHeartbeatRxTime = std::chrono::steady_clock::time_point::min();
      osctx->setBroadcasting();
    }
  });

  heartbeatInterval.start();
}

void OscReceiver::ProcessMessage(
  const osc::ReceivedMessage& message,
  const IpEndpointName& remoteEndpoint
) {
  DEBUG("oscrx received message on path %s", message.AddressPattern());

  try {
    std::string address = message.AddressPattern();
    if (!routes.count(address)) throw osc::Exception("no route for address");

    osc::ReceivedMessage::const_iterator argsIterator = message.ArgumentsBegin();
    routes.at(address)(argsIterator, remoteEndpoint);

    if (argsIterator != message.ArgumentsEnd()) throw osc::ExcessArgumentException();
  } catch(osc::Exception& e) {
    WARN("error parsing OSC message %s: %s", message.AddressPattern(), e.what());
  }
}

void OscReceiver::generateRoutes() {
  routes.emplace(
    "/register",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName& remoteEndpoint) {
      char* ip = (char*)malloc(IpEndpointName::ADDRESS_STRING_LENGTH + 1);
      remoteEndpoint.AddressAsString(ip);
      osctx->setDirect(ip);
      free(ip);
    }
  );

  routes.emplace(
    "/keepalive",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::scoped_lock<std::mutex> lock(heartbeatMutex);
      missedHeartbeats = 0;
      lastHeartbeatRxTime = std::chrono::steady_clock::now();
    }
  );

  routes.emplace(
    "/ack_chunk",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int32_t chunkedId = (args++)->AsInt32();
      int32_t chunkNum = (args++)->AsInt32();
      chunkman->ack(chunkedId, chunkNum);
    }
  );

  routes.emplace(
    "/get/module_stubs",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      (void)args;

      ctrl->enqueueAction([&]() {
        ModuleStubsPacker* packer = new ModuleStubsPacker();
        std::vector<int64_t> moduleIds = APP->engine->getModuleIds();

        for (const auto& id : moduleIds) {
          rack::plugin::Model* model = APP->engine->getModule(id)->getModel();
          packer->addModule(id, model->plugin->slug, model->slug);
        }

        osctx->enqueueMessage(packer);
      });
    }
  );

  routes.emplace(
    "/get/module_structure",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();

      ctrl->enqueueAction([&]() {
        ModuleStructurePacker* packer =
          new ModuleStructurePacker(pluginSlug, moduleSlug);
        osctx->enqueueMessage(packer);
      });
    }
  );

  routes.emplace(
    "/set/param/value",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();
      int32_t paramId = (args++)->AsInt32();
      float value = (args++)->AsFloat();

      ctrl->enqueueAction([moduleId, paramId, value]() {
        APP->scene->rack->getModule(moduleId)->
          getParam(paramId)->getParamQuantity()->setValue(value);
      });
    }
  );

  routes.emplace(
    "/patch/open",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string path = (args++)->AsString();
      ctrl->enqueueAction([&, path]() {
        SceneAction::Create([&, path]() {
          APP->patch->load(path);
          APP->patch->path = path;
          APP->history->setSaved();
        });
      });
    }
  );

  // TEMPLATE
  // routes.emplace(
  //   "",
  //   [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName& remoteEndpoint) {
  //     ctrl->enqueueAction([&]() {
  //     });
  //   }
  // );
}
