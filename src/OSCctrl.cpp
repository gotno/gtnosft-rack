#include "OSCctrl.hpp"

#include "osc/OscSender.hpp"
#include "osc/OscReceiver.hpp"
#include "osc/ChunkedManager.hpp"
#include "osc/SubscriptionManager.hpp"

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
  // txLight->borderColor = lightBgColor;
  addChild(txLight);

  RectangleLight<WhiteLight>* hbInLight =
    createLight<RectangleLight<WhiteLight>>(
      mm2px(Vec(9.220, 20.369)),
      module,
      OSCctrl::HEARTBEAT_IN_LIGHT
    );
  hbInLight->box.size = mm2px(hb_light_size);
  hbInLight->bgColor = lightBgColor;
  // hbInLight->borderColor = lightBgColor;
  addChild(hbInLight);

  RectangleLight<WhiteLight>* hbOutLight =
    createLight<RectangleLight<WhiteLight>>(
      mm2px(Vec(17.220, 20.369)),
      module,
      OSCctrl::HEARTBEAT_OUT_LIGHT
    );
  hbOutLight->box.size = mm2px(hb_light_size);
  hbOutLight->bgColor = lightBgColor;
  // hbOutLight->borderColor = lightBgColor;
  addChild(hbOutLight);

  if (!module) return;

  // bail if OSCctrl is already in the patch
  std::vector<int64_t> moduleIds = APP->engine->getModuleIds();
  for (const auto& id : moduleIds) {
    if (id == getModule()->getId()) {
      continue;
    }

    rack::plugin::Model* model = APP->engine->getModule(id)->getModel();
    if (model->plugin->slug == "gtnosft" && model->slug == "OSCctrl") {
      return;
    }
  }

  osctx = new OscSender(this);
  chunkman = new ChunkedManager(this, osctx);
  subman = new SubscriptionManager(this, osctx, chunkman);
  oscrx = new OscReceiver(this, osctx, chunkman, subman);
}

OSCctrlWidget::~OSCctrlWidget() {
  if (oscrx) delete oscrx;
  if (subman) delete subman;
  if (chunkman) delete chunkman;
  if (osctx) delete osctx;
}

void OSCctrlWidget::step() {
  ModuleWidget::step();
  if (!module) return;

  processActionQueue();
}

void OSCctrlWidget::enqueueAction(Action action) {
  std::lock_guard<std::mutex> locker(actionMutex);
  actionQueue.push(action);
}

void OSCctrlWidget::processActionQueue() {
  std::lock_guard<std::mutex> locker(actionMutex);
  while (!actionQueue.empty()) {
    auto action = actionQueue.front();
    action();
    actionQueue.pop();
  }
}

Model* modelOSCctrl = createModel<OSCctrl, OSCctrlWidget>("OSCctrl");
