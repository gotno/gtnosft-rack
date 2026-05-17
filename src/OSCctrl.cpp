#include "OSCctrl.hpp"

#include "osc/OscSender.hpp"
#include "osc/OscReceiver.hpp"
#include "osc/ChunkedManager.hpp"
#include "osc/SubscriptionManager.hpp"

#include "osc/ChunkedSend/ChunkedImage.hpp"

struct OSCctrl : Module {
  enum ParamId {
    PARAMS_LEN
  };
  enum InputId {
    INPUTS_LEN
  };
  enum OutputId {
    OUTPUTS_LEN
  };
  enum LightId {
    LIGHTS_LEN
  };

  OSCctrl() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
  }

  void process(const ProcessArgs& args) override {
  }
};

OSCctrlWidget::OSCctrlWidget(OSCctrl* module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/OSCctrl.svg")));
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

  osctx = new OscSender();
  chunkman = new ChunkedManager(this, osctx);
  subman = new SubscriptionManager(this, osctx, chunkman);
  oscrx = new OscReceiver(this, osctx, chunkman, subman);
}

OSCctrlWidget::~OSCctrlWidget() {
  delete oscrx;
  delete subman;
  delete chunkman;
  delete osctx;
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
