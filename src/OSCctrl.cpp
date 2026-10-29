#include "OSCctrl.hpp"

#include "osc/OscSender.hpp"
#include "osc/OscReceiver.hpp"
#include "osc/ChunkedManager.hpp"
#include "osc/SubscriptionManager.hpp"
#include "osc/Bundler/ModuleCacheGuard.hpp"
#include "bCast.hpp"
#include "osc/Bundler/AudioBundlers.hpp"
#include "texture/Renderer.hpp"
#include "util/Util.hpp"
#include "bench/Bench.hpp"

OSCctrl::OSCctrl() {
  config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

  configLight(TX_LIGHT, "TX");
  configLight(HEARTBEAT_OUT_LIGHT, "Heartbeat Out");
  configLight(HEARTBEAT_IN_LIGHT, "Heartbeat In");

  lights[TX_LIGHT].setBrightness(0.f);
  lights[HEARTBEAT_IN_LIGHT].setBrightness(0.f);
  lights[HEARTBEAT_OUT_LIGHT].setBrightness(0.f);

  lightDivider.setDivision(16);
}

void OSCctrl::process(const ProcessArgs& args) {
  if (lightDivider.process()) {
    if (broadcasting) {
      lights[TX_LIGHT].setBrightnessSmooth(
        txPulse.process(args.sampleTime),
        args.sampleTime * lightDivider.getDivision(),
        12.f
      );

      bool delayedPulse =
        bcastEchoThreshold.process(
          1.f - lights[TX_LIGHT].getBrightness(),
          0.1f,
          0.3f
        );
      if (delayedPulse) {
        hbInPulse.trigger();
        hbOutPulse.trigger();
      }
    } else {
      lights[TX_LIGHT].setBrightnessSmooth(
        txPulse.process(args.sampleTime),
        args.sampleTime * lightDivider.getDivision()
      );
    }

    lights[HEARTBEAT_IN_LIGHT].setBrightnessSmooth(
      hbInPulse.process(args.sampleTime),
      args.sampleTime * lightDivider.getDivision(),
      8.f
    );
    lights[HEARTBEAT_OUT_LIGHT].setBrightnessSmooth(
      hbOutPulse.process(args.sampleTime),
      args.sampleTime * lightDivider.getDivision(),
      8.f
    );
  }
}

OSCctrlWidget::OSCctrlWidget(OSCctrl* module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/OSCctrl.svg")));

  rack::math::Vec tx_light_size(2.f, 1.f);
  rack::math::Vec hb_light_size(4.f, 1.f);
  NVGcolor lightBgColor = rack::color::fromHexString("#eb7355");

  RectangleLight<WhiteLight>* txLight =
    createLight<RectangleLight<WhiteLight>>(
      mm2px(Vec(14.220, 20.369)),
      module,
      OSCctrl::TX_LIGHT
    );
  txLight->box.size = mm2px(tx_light_size);
  txLight->bgColor = lightBgColor;
  addChild(txLight);

  RectangleLight<WhiteLight>* hbInLight =
    createLight<RectangleLight<WhiteLight>>(
      mm2px(Vec(9.220, 20.369)),
      module,
      OSCctrl::HEARTBEAT_IN_LIGHT
    );
  hbInLight->box.size = mm2px(hb_light_size);
  hbInLight->bgColor = lightBgColor;
  addChild(hbInLight);

  RectangleLight<WhiteLight>* hbOutLight =
    createLight<RectangleLight<WhiteLight>>(
      mm2px(Vec(17.220, 20.369)),
      module,
      OSCctrl::HEARTBEAT_OUT_LIGHT
    );
  hbOutLight->box.size = mm2px(hb_light_size);
  hbOutLight->bgColor = lightBgColor;
  addChild(hbOutLight);

  if (!module) return;

  // cancel init if OSCctrl is already in the patch
  std::string thisPlugin = module->getModel()->plugin->slug;
  std::string thisModule = module->getModel()->slug;
  std::vector<int64_t> moduleIds = APP->engine->getModuleIds();
  for (const auto& id : moduleIds) {
    if (id == getModule()->getId()) {
      continue;
    }

    rack::plugin::Model* model = APP->engine->getModule(id)->getModel();
    if (model->plugin->slug == thisPlugin && model->slug == thisModule) {
      return;
    }
  }

  osctx = new OscSender(this);
  chunkman = new ChunkedManager(osctx);
  subman = new SubscriptionManager(this, osctx, chunkman);
  oscrx = new OscReceiver(this, osctx, chunkman, subman);
}

OSCctrlWidget::~OSCctrlWidget() {
  bool isPrimaryOSCctrl = osctx != NULL;

  // hand the expander back to its own settings
  if (isPrimaryOSCctrl) {
    if (bCast* audio = managedAudio()) audio->setManaged(false);
  }

  // order is important:
  // * stop inbound traffic and heartbeat
  // * stop chunked prep worker, which enqueues to the sender
  // * stop send worker
  // * delete managers that queued bundlers call back into
  if (oscrx) delete oscrx;
  if (chunkman) chunkman->stopWorker();
  if (osctx) delete osctx;
  if (subman) delete subman;
  if (chunkman) delete chunkman;

  // clear static caches so a future OSCctrl instance starts with fresh state
  if (isPrimaryOSCctrl) ModuleCacheGuard::clearAll();
}

void OSCctrlWidget::step() {
  BENCH(auto stepStart = bench::clock::now();)
  ModuleWidget::step();
  if (!module) return;

  if (subman) subman->tick();
  if (chunkman) {
    BENCH(
      static bench::Tally& chunkTick = bench::tally("ui.chunk_tick");
      bench::ScopedTally tickTimer(chunkTick);
    )
    chunkman->tick();
  }

  processActionQueue();

  if (osctx) stepAudioExpander();

  if (chunkman) Renderer::evictIdleOverlays();

  BENCH(
    if (chunkman) {
      int64_t cacheBytes = 0;
      for (auto& [moduleId, entry] : Renderer::overlayCache)
        cacheBytes += entry.framebuffer->getFramebufferSize().area() * 4;
      bench::gauge("overlay_cache.entries", Renderer::overlayCache.size());
      bench::gauge("overlay_cache.bytes", cacheBytes);

      bench::recordFrame(
        APP->window->getLastFrameDuration(),
        std::chrono::duration<double>(bench::clock::now() - stepStart).count()
      );
    }
  )
}

bCast* OSCctrlWidget::managedAudio() {
  if (managedAudioId < 0) return nullptr;
  Module* m = APP->engine->getModule(managedAudioId);
  return (m && m->model == modelbCast) ? static_cast<bCast*>(m) : nullptr;
}

void OSCctrlWidget::stepAudioExpander() {
  int64_t foundId = -1;
  for (int64_t id : {module->leftExpander.moduleId, module->rightExpander.moduleId}) {
    if (id < 0) continue;
    Module* m = APP->engine->getModule(id);
    if (m && m->model == modelbCast) {
      foundId = id;
      break;
    }
  }
  if (foundId == managedAudioId) {
    // the preset can also change in Rack (knob or menu); tell the client now
    bCast* audio = managedAudio();
    if (audio && audio->preset() != announcedAudioPreset) announceAudioStream();
    return;
  }

  if (managedAudioId >= 0) {
    if (bCast* old = managedAudio()) old->setManaged(false);
    osctx->pacer.setActive(false, std::chrono::steady_clock::now());
    if (!osctx->isBroadcasting())
      osctx->enqueueBundler(new AudioStreamBundler(
        managedAudioId, (uint32_t)managedAudioId, bcast::DEFAULT_PRESET, false, 0
      ));
  }

  managedAudioId = foundId;
  if (bCast* audio = managedAudio()) {
    audio->setManaged(true);
    announceAudioStream();
  }
}

void OSCctrlWidget::announceAudioStream() {
  if (!osctx || osctx->isBroadcasting()) return;
  bCast* audio = managedAudio();
  if (!audio) return;
  bCastSnapshot snap = audio->snapshot();
  announcedAudioPreset = snap.config.preset;
  osctx->enqueueBundler(new AudioStreamBundler(
    audio->id,
    audio->streamId(),
    snap.config.preset,
    snap.config.enabled,
    audio->epoch.load()
  ));
}

void OSCctrlWidget::onClientRegistered() {
  if (bCast* audio = managedAudio()) audio->setManagedEnabled(false, "", 0);
  osctx->pacer.setActive(false, std::chrono::steady_clock::now());
  announceAudioStream();
}

void OSCctrlWidget::onClientGone() {
  if (bCast* audio = managedAudio()) audio->setManagedEnabled(false, "", 0);
  osctx->pacer.setActive(false, std::chrono::steady_clock::now());
}

void OSCctrlWidget::enqueueAction(Action action, const char* tag) {
  std::lock_guard<std::mutex> locker(actionMutex);
  actionQueue.push(QueuedAction{
    std::move(action),
    tag
    BENCH(, bench::clock::now())
  });
}

void OSCctrlWidget::processActionQueue() {
  {
    std::lock_guard<std::mutex> locker(actionMutex);
    while (!actionQueue.empty()) {
      pendingActions.push_back(std::move(actionQueue.front()));
      actionQueue.pop();
    }
  }

  if (pendingActions.empty()) return;

  bool budgeted = true;
  BENCH(
    budgeted = actionBudgetEnabled.load();

    // UI thread only. caches tally lookups so we don't take the bench mutex
    // per action
    static std::map<const char*, bench::Tally*> tagTallies;

    size_t ran = 0;
    float maxWaitMs = 0.f;
    bench::gauge("ui.action_queue.depth", pendingActions.size());
  )

  // actions enqueued while these run wait for the next frame
  size_t available = pendingActions.size();
  auto batchStart = std::chrono::steady_clock::now();

  while (available > 0) {
    QueuedAction queued = std::move(pendingActions.front());
    pendingActions.pop_front();
    --available;

    BENCH(
      bench::time_point actionStart = bench::clock::now();
      maxWaitMs = std::max(maxWaitMs, std::chrono::duration<float, std::milli>(
        actionStart - queued.enqueuedAt
      ).count());
    )

    queued.action();

    BENCH(
      ++ran;
      bench::Tally*& tally = tagTallies[queued.tag];
      if (!tally)
        tally = &bench::tally((std::string("ui.action.") + queued.tag).c_str());
      tally->add(std::chrono::duration_cast<std::chrono::nanoseconds>(
        bench::clock::now() - actionStart
      ).count());
    )

    if (
      budgeted && available > 0
        && std::chrono::steady_clock::now() - batchStart >= ACTION_BUDGET
    ) {
      BENCH(bench::count("ui.action_budget.exceeded");)
      break;
    }
  }

  BENCH(
    bench::sample("ui.actions_per_frame", (float)ran);
    bench::sample("ui.actions_ms", std::chrono::duration<float, std::milli>(
      bench::clock::now() - batchStart
    ).count());
    bench::sample("ui.action_wait", maxWaitMs);
  )
}

void OSCctrlWidget::appendContextMenu(Menu* menu) {
  BENCH(
    menu->addChild(new MenuSeparator);

    menu->addChild(createMenuItem("Reset bench", "", [=]() {
      bench::reset();
    }));

    menu->addChild(createMenuItem("Dump bench report", "", [=]() {
      bench::logReport();
    }));
  )

  menu->addChild(new MenuSeparator);
  menu->addChild(createSubmenuItem("Render", "",
    [=](Menu* menu) {
      for (int64_t moduleId : APP->engine->getModuleIds()) {
        rack::engine::Module* module = APP->engine->getModule(moduleId);
        if (!module || !module->getModel()) continue;

        std::string pluginSlug = module->getModel()->plugin->slug;
        std::string moduleSlug = module->getModel()->slug;
        std::string moduleName = rack::string::f(
          "%lld-%s-%s",
          (long long)moduleId,
          pluginSlug.c_str(),
          moduleSlug.c_str()
        );

        menu->addChild(createSubmenuItem(moduleName.c_str(), "",
          [=](Menu* menu) {
            auto renderModule = [this, moduleId, pluginSlug, moduleSlug](
              bool panel
            ) {
              enqueueAction([moduleId, pluginSlug, moduleSlug, panel]() {
                rack::plugin::Model* model =
                  gtnosft::util::findModel(pluginSlug, moduleSlug);
                rack::app::ModuleWidget* moduleWidget =
                  gtnosft::util::makeModuleWidget(model);
                if (!moduleWidget) {
                  WARN(
                    "OSCctrl render failed to create widget for %s:%s",
                    pluginSlug.c_str(),
                    moduleSlug.c_str()
                  );
                  return;
                }

                RenderResult result = panel
                  ? Renderer::renderPanel(moduleWidget, Recipe(1024))
                  : Renderer::renderModule(moduleWidget, Recipe(1024));
                delete moduleWidget;

                if (!result.success()) {
                  WARN(
                    "OSCctrl render failed for %lld-%s-%s-%s: %s",
                    (long long)moduleId,
                    pluginSlug.c_str(),
                    moduleSlug.c_str(),
                    panel ? "Panel" : "Module",
                    result.statusMessage.c_str()
                  );
                  return;
                }

                Renderer::renderPng(
                  result.pixels,
                  result.width,
                  result.height,
                  "oscctrl_render",
                  rack::string::f(
                    "%lld-%s-%s-%s",
                    (long long)moduleId,
                    pluginSlug.c_str(),
                    moduleSlug.c_str(),
                    panel ? "Panel" : "Module"
                  )
                );
                delete[] result.pixels;
              },
              "context_render"
            );
          };

          menu->addChild(createMenuItem("Module", "", [renderModule]() {
            renderModule(false);
          }));
          menu->addChild(createMenuItem("Panel", "", [renderModule]() {
            renderModule(true);
          }));
          menu->addChild(createMenuItem("Overlay", "", [this, moduleId,
            pluginSlug, moduleSlug]() {
            enqueueAction([moduleId, pluginSlug, moduleSlug]() {
              RenderResult result =
                Renderer::renderOverlay(moduleId, Recipe(1024));
              if (!result.success()) {
                WARN(
                  "OSCctrl render failed for %lld-%s-%s-Overlay: %s",
                  (long long)moduleId,
                  pluginSlug.c_str(),
                  moduleSlug.c_str(),
                  result.statusMessage.c_str()
                );
                return;
              }

              Renderer::renderPng(
                result.pixels,
                result.width,
                result.height,
                "oscctrl_render",
                rack::string::f(
                  "%lld-%s-%s-Overlay",
                  (long long)moduleId,
                  pluginSlug.c_str(),
                  moduleSlug.c_str()
                )
              );
              delete[] result.pixels;
            }, "context_render");
          }));
        }));
      }
    }
  ));
}

Model* modelOSCctrl = createModel<OSCctrl, OSCctrlWidget>("OSCctrl");
