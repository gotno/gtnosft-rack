#include "app/ModuleWidget.hpp"
#include "color.hpp"
#include "rack.hpp"
#include "patch.hpp"

#include "OscReceiver.hpp"

#include "../OSCctrl.hpp"
#include "OscSender.hpp"
#include "ChunkedManager.hpp"
#include "SubscriptionManager.hpp"

#include "ChunkedSend/ChunkedImage.hpp"

#include "Bundler/PatchInfoBundler.hpp"
#include "Bundler/ModuleStubsBundler.hpp"
#include "Bundler/ModuleStructureBundler.hpp"
#include "Bundler/ModuleStateBundler.hpp"
#include "Bundler/ModuleParamsBundler.hpp"
#include "Bundler/CablesBundler.hpp"

#include "../renderer/Renderer.hpp"

OscReceiver::OscReceiver(
  OSCctrlWidget* _ctrl,
  OscSender* oscSender,
  ChunkedManager* chunkedManager,
  SubscriptionManager* subscriptionManager
): ctrl(_ctrl),
  osctx(oscSender),
  chunkman(chunkedManager),
  subman(subscriptionManager),
  endpoint(IpEndpointName(IpEndpointName::ANY_ADDRESS, RX_PORT)) {
    generateRoutes();
    startListener();
    startHeartbeat();
  }

OscReceiver::~OscReceiver() {
  endListener();
}

void OscReceiver::startListener() {
  try {
    rxSocket = new UdpListeningReceiveSocket(endpoint, this);
    listenerThread = std::thread(&UdpListeningReceiveSocket::Run, rxSocket);
  } catch (const std::runtime_error& e) {
    if (activePort >= RX_PORT + maxBindRetries) {
      WARN("failed to start OSC receiver after %d attempts", maxBindRetries);
      return;
    }

    WARN("error starting OSC receiver: %s", e.what());

    ++activePort;
    endpoint = IpEndpointName(IpEndpointName::ANY_ADDRESS, activePort);

    startListener();
    return;
  }

  INFO("OSCctrl started OSC receiver on port %d", activePort);
}

void OscReceiver::endListener() {
  if (rxSocket == NULL) return;

  rxSocket->AsynchronousBreak();
  listenerThread.join();
  delete rxSocket;
  rxSocket = NULL;
}

void OscReceiver::startHeartbeat() {
  heartbeatInterval = Timer::setInterval(HEARTBEAT_DELAY, [this] {
    ctrl->enqueueAction([this]() {
      osctx->sendHeartbeat();
    });

    std::scoped_lock<std::mutex> lock(heartbeatMutex);

    // client isn't sending heartbeats or hasn't started yet
    if (lastHeartbeatRxTime == std::chrono::steady_clock::time_point::min())
      return;

    auto now = std::chrono::steady_clock::now();
    auto delayMs = std::chrono::milliseconds(HEARTBEAT_DELAY);

    if (lastHeartbeatRxTime < (now - delayMs)) ++missedHeartbeats;

    // client is gone, switch back to broadcasting
    if (missedHeartbeats >= maxMissedHeartbeats) {
      WARN("client is gone, switching back to broadcasting");
      lastHeartbeatRxTime = std::chrono::steady_clock::time_point::min();
      osctx->setBroadcasting();

      subman->reset();
    }
  });

  heartbeatInterval.start();
}

void OscReceiver::ProcessMessage(
  const osc::ReceivedMessage& message,
  const IpEndpointName& remoteEndpoint
) {
  // DEBUG("oscrx received message on path %s", message.AddressPattern());

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

bool OscReceiver::floatOrInt32(
  osc::ReceivedMessage::const_iterator& arg,
  float& outFloat,
  int32_t& outInt32
) {
  outFloat = -1.f;
  if (arg->IsFloat()) {
    outFloat = arg->AsFloat();
  }

  outInt32 = -1;
  if (arg->IsInt32()) {
    outInt32 = arg->AsInt32();
  }

  arg++;

  return outFloat != -1.f || outInt32 != -1;
}

void OscReceiver::generateRoutes() {
  routes.emplace(
    "/register",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName& remoteEndpoint) {
      char* ip = (char*)malloc(IpEndpointName::ADDRESS_STRING_LENGTH + 1);
      remoteEndpoint.AddressAsString(ip);
      osctx->setDirect(ip);
      free(ip);

      subman->start();
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
    "/get/patch_info",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      (void)args;

      ctrl->enqueueAction([&]() {
        osctx->enqueueBundler(new PatchInfoBundler());
      });
    }
  );

  routes.emplace(
    "/get/module_stubs",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      (void)args;

      ctrl->enqueueAction([&]() {
        osctx->enqueueBundler(new ModuleStubsBundler());
      });
    }
  );

  routes.emplace(
    "/get/cables",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      (void)args;

      ctrl->enqueueAction([&]() {
        osctx->enqueueBundler(new CablesBundler());
      });
    }
  );

  routes.emplace(
    "/get/module_structure",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();

      ctrl->enqueueAction([this, pluginSlug, moduleSlug]() {
        ModuleStructureBundler* bundler =
          new ModuleStructureBundler(pluginSlug, moduleSlug);
        osctx->enqueueBundler(bundler);
      });
    }
  );

  // textures
  routes.emplace(
    "/get/texture/panel",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();

      float scale;
      int32_t height;
      if (!floatOrInt32(args, scale, height)) {
        INFO(
          "/get/texture/panel %s:%s received neither scale (float) nor height (int32)",
          pluginSlug.c_str(),
          moduleSlug.c_str()
        );
        return;
      }

      int32_t requestedId = (args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        std::variant<float, int32_t> scaleOrHeight;
        if (scale != -1.f) {
          scaleOrHeight = scale;
        } else {
          scaleOrHeight = height;
        }

        RenderResult render =
          Renderer::renderPanel(pluginSlug, moduleSlug, scaleOrHeight);

        if (render.failure()) {
          INFO(
            "failed to render panel %s:%s",
            pluginSlug.c_str(),
            moduleSlug.c_str()
          );
          INFO("  %s", render.statusMessage.c_str());
          return;
        }

        ChunkedImage* chunkedImage = new ChunkedImage(render);
        chunkedImage->id = requestedId;
        chunkman->add(chunkedImage);
      });
    }
  );

  routes.emplace(
    "/get/texture/overlay",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();

      float scale;
      int32_t height;
      if (!floatOrInt32(args, scale, height)) {
        INFO(
          "/get/texture/overlay %lld received neither scale (float) nor height (int32)",
          moduleId
        );
        return;
      }

      int32_t forceRender = (args++)->AsBool();
      int32_t requestedId = (args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        if (chunkman->isProcessing(requestedId) && !forceRender) return;

        std::variant<float, int32_t> scaleOrHeight;
        if (scale != -1.f) {
          scaleOrHeight = scale;
        } else {
          scaleOrHeight = height;
        }

        RenderResult render = Renderer::renderOverlay(moduleId, scaleOrHeight);
        if (render.failure()) {
          INFO("failed to render overlay %lld", moduleId);
          INFO("  %s", render.statusMessage.c_str());
          return;
        }

        ChunkedImage* chunkedImage = new ChunkedImage(render);
        chunkedImage->id = requestedId;
        chunkman->add(chunkedImage, true);
      });
    }
  );

  routes.emplace(
    "/get/texture/port",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();
      int32_t portId = (args++)->AsInt32();
      PortType portType = (PortType)(args++)->AsInt32();

      float scale;
      int32_t height;
      if (!floatOrInt32(args, scale, height)) {
        INFO(
          "/get/texture/port %s:%s:%d(%s) received neither scale (float) nor height (int32)",
          pluginSlug.c_str(),
          moduleSlug.c_str(),
          portId,
          portType == PortType::Input ? "input" : "output"
        );
        return;
      }

      int32_t requestedId = (args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        std::variant<float, int32_t> scaleOrHeight;
        if (scale == -1.f) {
          scaleOrHeight = height;
        } else {
          scaleOrHeight = scale;
        }

        RenderResult render =
          Renderer::renderPort(
            pluginSlug,
            moduleSlug,
            portId,
            portType == PortType::Input
              ? rack::engine::Port::INPUT
              : rack::engine::Port::OUTPUT,
            scaleOrHeight
          );

        if (render.failure()) {
          INFO("failed to render port %s:%s:%d", pluginSlug.c_str(), moduleSlug.c_str(), portId);
          INFO("  %s", render.statusMessage.c_str());
          return;
        }

        ChunkedImage* chunkedImage = new ChunkedImage(render);
        chunkedImage->id = requestedId;
        chunkman->add(chunkedImage);
      });
    }
  );

  routes.emplace(
    "/get/texture/knob",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();
      int paramId = (int)(args++)->AsInt32();

      float scale;
      int32_t height;
      if (!floatOrInt32(args, scale, height)) {
        INFO(
          "/get/texture/knob %s:%s:%d received neither scale (float) nor height (int32)",
          pluginSlug.c_str(),
          moduleSlug.c_str(),
          paramId
        );
        return;
      }

      int bgId = (int)(args++)->AsInt32();
      int mgId = (int)(args++)->AsInt32();
      int fgId = (int)(args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        std::map<std::string, RenderResult> renderResults;

        std::variant<float, int32_t> scaleOrHeight;
        if (scale != -1.f) {
          scaleOrHeight = scale;
        } else {
          scaleOrHeight = height;
        }

        RenderResult result =
          Renderer::renderKnob(
            pluginSlug,
            moduleSlug,
            paramId,
            renderResults,
            scaleOrHeight
          );

        if (result.failure()) {
          INFO(
            "failed to render knob %s:%s:%d",
            pluginSlug.c_str(), moduleSlug.c_str(), paramId
          );
          INFO("  %s", result.statusMessage.c_str());
          return;
        }

        if (renderResults.contains("bg") && !renderResults.at("bg").failure()) {
          ChunkedImage* chunkedImage = new ChunkedImage(renderResults.at("bg"));
          chunkedImage->id = bgId;
          chunkman->add(chunkedImage);
        }

        if (renderResults.contains("mg") && !renderResults.at("mg").failure()) {
          ChunkedImage* chunkedImage = new ChunkedImage(renderResults.at("mg"));
          chunkedImage->id = mgId;
          chunkman->add(chunkedImage);
        }

        if (renderResults.contains("fg") && !renderResults.at("fg").failure()) {
          ChunkedImage* chunkedImage = new ChunkedImage(renderResults.at("fg"));
          chunkedImage->id = fgId;
          chunkman->add(chunkedImage);
        }
      });
    }
  );

  routes.emplace(
    "/get/texture/switch",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();
      int paramId = (int)(args++)->AsInt32();

      float scale;
      int32_t height;
      if (!floatOrInt32(args, scale, height)) {
        INFO(
          "/get/texture/switch %s:%s:%d received neither scale (float) nor height (int32)",
          pluginSlug.c_str(),
          moduleSlug.c_str(),
          paramId
        );
        return;
      }

      std::vector<int32_t> requestedIds;

      int32_t requestedId;
      try {
        while(true) {
          requestedId = (args++)->AsInt32();
          requestedIds.push_back(requestedId);
        }
      } catch (const osc::WrongArgumentTypeException& e) {}

      ctrl->enqueueAction([=, this]() {
        std::vector<RenderResult> renderResults;

        std::variant<float, int32_t> scaleOrHeight;
        if (scale != -1.f) {
          scaleOrHeight = scale;
        } else {
          scaleOrHeight = height;
        }

        RenderResult result =
          Renderer::renderSwitch(
            pluginSlug,
            moduleSlug,
            paramId,
            renderResults,
            scaleOrHeight
          );

        for (size_t i = 0; i < renderResults.size(); ++i) {
          if (renderResults[i].failure()) {
            INFO(
              "failed to render switch %s:%s:%d",
              pluginSlug.c_str(), moduleSlug.c_str(), paramId
            );
            INFO("  %s", renderResults[i].statusMessage.c_str());
            continue;
          }

          if (i >= requestedIds.size()) {
            WARN("/get/texture/switch more switch textures than requested ids");
            break;
          }

          ChunkedImage* chunkedImage = new ChunkedImage(renderResults[i]);
          chunkedImage->id = requestedIds[i];
          chunkman->add(chunkedImage);
        }
      });
    }
  );

  routes.emplace(
    "/get/texture/slider",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      std::string pluginSlug = (args++)->AsString();
      std::string moduleSlug = (args++)->AsString();
      int paramId = (int)(args++)->AsInt32();

      float scale;
      int32_t height;
      if (!floatOrInt32(args, scale, height)) {
        INFO(
          "/get/texture/slider %s:%s:%d received neither scale (float) nor height (int32)",
          pluginSlug.c_str(),
          moduleSlug.c_str(),
          paramId
        );
        return;
      }

      int trackId = (int)(args++)->AsInt32();
      int handleId = (int)(args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        std::map<std::string, RenderResult> renderResults;

        std::variant<float, int32_t> scaleOrHeight;
        if (scale != -1.f) {
          scaleOrHeight = scale;
        } else {
          scaleOrHeight = height;
        }

        RenderResult result =
          Renderer::renderSlider(
            pluginSlug,
            moduleSlug,
            paramId,
            renderResults,
            scaleOrHeight
          );

        if (result.failure()) {
          INFO(
            "failed to render slider %s:%s:%d",
            pluginSlug.c_str(), moduleSlug.c_str(), paramId
          );
          INFO("  %s", result.statusMessage.c_str());
          return;
        }

        if (renderResults.contains("track") && !renderResults.at("track").failure()) {
          ChunkedImage* chunkedImage = new ChunkedImage(renderResults.at("track"));
          chunkedImage->id = trackId;
          chunkman->add(chunkedImage);
        }

        if (renderResults.contains("handle") && !renderResults.at("handle").failure()) {
          ChunkedImage* chunkedImage = new ChunkedImage(renderResults.at("handle"));
          chunkedImage->id = handleId;
          chunkman->add(chunkedImage);
        }
      });
    }
  );

  // state
  routes.emplace(
    "/get/module_state",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();

      ctrl->enqueueAction([this, moduleId]() {
        osctx->enqueueBundler(
          new ModuleStateBundler(moduleId, ctrl->box)
        );
      });
    }
  );

  routes.emplace(
    "/get/params_state",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();

      ctrl->enqueueAction([this, moduleId]() {
        std::vector<int64_t> moduleIds = {moduleId};

        osctx->enqueueBundler(
          new ModuleParamsBundler(moduleIds)
        );
      });
    }
  );

  // subscriptions
  routes.emplace(
    "/subscribe/module/lights",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();

      ctrl->enqueueAction([this, moduleId]() {
        subman->subscribeModuleLights(moduleId);
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
        rack::app::ModuleWidget* module =
          APP->scene->rack->getModule(moduleId);
        if (!module) return;

        rack::app::ParamWidget* param = module->getParam(paramId);
        if (!param) return;

        param->getParamQuantity()->setValue(value);
      });
    }
  );

  routes.emplace(
    "/add/cable",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t inputModuleId = (args++)->AsInt64();
      int64_t outputModuleId = (args++)->AsInt64();
      int32_t inputPortId = (args++)->AsInt32();
      int32_t outputPortId = (args++)->AsInt32();

      std::string color = (args++)->AsString();

      int64_t returnId = -1;
      try {
        returnId = (args++)->AsInt64();
      } catch (const osc::WrongArgumentTypeException& e) {}

      ctrl->enqueueAction([=, this]() {
        // TODO: history (see PortWidget::dragStart/dragEnd for example)
        rack::app::ModuleWidget* inputModule =
          APP->scene->rack->getModule(inputModuleId);
        if (!inputModule) return;
        rack::app::ModuleWidget* outputModule =
          APP->scene->rack->getModule(outputModuleId);
        if (!outputModule) return;

        rack::app::PortWidget* inputPort = inputModule->getInput(inputPortId);
        if (!inputPort) return;
        rack::app::PortWidget* outputPort = outputModule->getOutput(outputPortId);
        if (!outputPort) return;

        rack::app::CableWidget* cableWidget = new rack::app::CableWidget;
        cableWidget->inputPort = inputPort;
        cableWidget->outputPort = outputPort;
        cableWidget->color = rack::color::fromHexString(color);

        // updateCable handles creating and adding the engine::Cable
        cableWidget->updateCable();
        APP->scene->rack->addCable(cableWidget);

        osctx->enqueueBundler(
          new CablesBundler(cableWidget->getCable()->id, returnId)
        );
      });
    }
  );

  routes.emplace(
    "/remove/cable",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t cableId = (args++)->AsInt64();

      ctrl->enqueueAction([cableId]() {
        // TODO: history (see PortWidget::dragStart/dragEnd for example)
        rack::app::CableWidget* cw =
          APP->scene->rack->getCable(cableId);
        if (!cw) return;

        APP->scene->rack->removeCable(cw);
        delete cw;
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
