#include "bCast.hpp"
#include "audio/AudioStreamer.hpp"
#include "audio/EngineClock.hpp"

#include <cmath>
#include <cstdlib>

bCast::bCast() {
  config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
  configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "Level", " dB", -10, 40);
  std::vector<std::string> presetNames;
  for (const bcast::Preset& p : bcast::PRESETS) presetNames.push_back(p.name);
  configSwitch(PRESET_PARAM, 0.f, bcast::PRESET_COUNT - 1, bcast::DEFAULT_PRESET, "Preset", presetNames);
  paramQuantities[PRESET_PARAM]->randomizeEnabled = false;
  configInput(LEFT_INPUT, "Left");
  configInput(RIGHT_INPUT, "Right");
  configLight(STATUS_LIGHT, "Status");
  lightDivider.setDivision(512);
  for (auto& f : dcFilters) f.setCutoffFreq(10.f / 48000.f);
  // seed the epoch per instance so a recreated bCast with the same module id
  // doesn't restart at 0 and have its packets dropped as stragglers
  epoch = (uint16_t)rack::random::u64();
  engineClock = std::make_unique<bcast::EngineClock>(this);
}

bCast::~bCast() {
  engineClock.reset();
  stopStreamer();
}

bool bCast::isMaster() {
  return APP->engine->getMasterModule() == this;
}

void bCast::setMaster(bool on) {
  if (on) APP->engine->setMasterModule(this);
  else if (isMaster()) APP->engine->setMasterModule(NULL);
}

void bCast::stopStreamer() {
  if (!streamer) return;
  streamer->stop();
  streamer.reset();
}

void bCast::onAdd(const AddEvent& e) {
  if (!streamer) streamer = std::make_unique<bcast::AudioStreamer>(this);
  streamer->start();
}

void bCast::onRemove(const RemoveEvent& e) {
  stopStreamer();
}

void bCast::onSampleRateChange(const SampleRateChangeEvent& e) {
  for (auto& f : dcFilters) f.setCutoffFreq(10.f * e.sampleTime);
  engineSampleRate = e.sampleRate;
}

void bCast::process(const ProcessArgs& args) {
  const float gain = std::pow(params[LEVEL_PARAM].getValue(), 2.f);
  const bool rightConnected = inputs[RIGHT_INPUT].isConnected();

  dsp::Frame<2> frame;
  for (int c = 0; c < 2; ++c) {
    int input = (c == 1 && rightConnected) ? RIGHT_INPUT : LEFT_INPUT;
    float v = inputs[input].getVoltageSum() / 10.f;
    if (!std::isfinite(v)) v = 0.f;
    if (dcFilterEnabled) {
      dcFilters[c].process(v);
      v = dcFilters[c].highpass();
    }
    v *= gain;
    frame.samples[c] = v;
    vuMeters[c].process(args.sampleTime, v);
  }

  if (streaming.load(std::memory_order_relaxed)) {
    if (ring.full()) ringOverflows.fetch_add(1, std::memory_order_relaxed);
    else ring.push(frame);
  }

  if (lightDivider.process()) {
    syncPresetKnob();

    float r = 0.f, g = 0.f, b = 0.f;
    bool on = streaming.load(std::memory_order_relaxed);
    if (sendFailing.load(std::memory_order_relaxed)) r = 1.f;
    else if (managed.load(std::memory_order_relaxed)) b = on ? 1.f : 0.3f;
    else if (on) g = 1.f;
    lights[STATUS_LIGHT + 0].setBrightness(r);
    lights[STATUS_LIGHT + 1].setBrightness(g);
    lights[STATUS_LIGHT + 2].setBrightness(b);

    for (int c = 0; c < 2; ++c) {
      int base = VU_LIGHTS + c * 6;
      lights[base + 0].setBrightness(vuMeters[c].getBrightness(0, 0));
      lights[base + 1].setBrightness(vuMeters[c].getBrightness(-6, -3));
      lights[base + 2].setBrightness(vuMeters[c].getBrightness(-12, -6));
      lights[base + 3].setBrightness(vuMeters[c].getBrightness(-24, -12));
      lights[base + 4].setBrightness(vuMeters[c].getBrightness(-36, -24));
      lights[base + 5].setBrightness(vuMeters[c].getBrightness(-48, -36));
    }
  }
}

// a turned knob sets the preset; otherwise the knob follows the preset
void bCast::syncPresetKnob() {
  int knob = (int)std::round(params[PRESET_PARAM].getValue());
  int current = preset();
  if (lastPresetKnob >= 0 && knob != lastPresetKnob) {
    lastPresetKnob = knob;
    setPreset(knob);
  } else {
    if (knob != current) params[PRESET_PARAM].setValue((float)current);
    lastPresetKnob = current;
  }
}

void bCast::update(const std::function<void()>& change) {
  std::lock_guard<std::mutex> lock(configMutex);
  bCastConfig before = effective();
  change();
  presetCache = effective().preset;
  if (!(effective() == before)) ++version;
}

void bCast::setPreset(int preset) {
  update([&]() {
    if (!bcast::isValidPreset(preset)) return;
    standalone.preset = preset;
    managedConfig.preset = preset;
  });
}

bCastSnapshot bCast::snapshot() {
  std::lock_guard<std::mutex> lock(configMutex);
  return {effective(), managed.load(), version};
}

bCastConfig bCast::standaloneConfig() {
  std::lock_guard<std::mutex> lock(configMutex);
  return standalone;
}

void bCast::setStandalone(const std::function<void(bCastConfig&)>& edit) {
  update([&]() { edit(standalone); });
}

void bCast::setManaged(bool on) {
  update([&]() {
    if (on && !managed) {
      managedConfig.enabled = false;
      managedConfig.preset = standalone.preset;
      stats = {};
    }
    managed = on;
  });
}

void bCast::setManagedEnabled(bool enabled, const std::string& host, int port) {
  update([&]() {
    if (!managed) return;
    managedConfig.enabled = enabled;
    if (enabled) {
      managedConfig.host = host;
      managedConfig.port = port;
    }
  });
}

void bCast::setManagedPreset(int preset) {
  update([&]() {
    if (!managed || !bcast::isValidPreset(preset)) return;
    standalone.preset = preset;
    managedConfig.preset = preset;
  });
}

void bCast::setClientStats(const bCastClientStats& s) {
  std::lock_guard<std::mutex> lock(configMutex);
  stats = s;
  stats.valid = true;
}

bCastClientStats bCast::clientStats() {
  std::lock_guard<std::mutex> lock(configMutex);
  return stats;
}

json_t* bCast::dataToJson() {
  bCastConfig c = standaloneConfig();
  json_t* rootJ = json_object();
  json_object_set_new(rootJ, "dcFilter", json_boolean(dcFilterEnabled));
  json_object_set_new(rootJ, "enabled", json_boolean(c.enabled));
  json_object_set_new(rootJ, "preset", json_integer(c.preset));
  json_object_set_new(rootJ, "host", json_string(c.host.c_str()));
  json_object_set_new(rootJ, "port", json_integer(c.port));
  return rootJ;
}

void bCast::dataFromJson(json_t* rootJ) {
  if (json_t* j = json_object_get(rootJ, "dcFilter")) dcFilterEnabled = json_boolean_value(j);
  setStandalone([&](bCastConfig& c) {
    if (json_t* j = json_object_get(rootJ, "enabled")) c.enabled = json_boolean_value(j);
    if (json_t* j = json_object_get(rootJ, "preset")) {
      int preset = (int)json_integer_value(j);
      if (bcast::isValidPreset(preset)) c.preset = preset;
    }
    if (json_t* j = json_object_get(rootJ, "host")) {
      const char* host = json_string_value(j);
      if (host && *host) c.host = host;
    }
    if (json_t* j = json_object_get(rootJ, "port")) {
      int port = (int)json_integer_value(j);
      if (port > 0 && port < 65536) c.port = port;
    }
  });
}

namespace {

// commits on Enter, then closes the menu
struct ConfigField : ui::TextField {
  std::function<void(const std::string&)> commit;

  ConfigField() { box.size.x = 140; }

  void onAction(const ActionEvent& e) override {
    if (commit) commit(text);
    if (ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>())
      overlay->requestDelete();
    e.consume(this);
  }
};

}

bCastWidget::bCastWidget(bCast* module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/bCast.svg")));

  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
    Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)
  ));

  addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
    mm2px(Vec(12.7, 16)), module, bCast::STATUS_LIGHT
  ));

  for (int c = 0; c < 2; ++c) {
    float x = c == 0 ? 8.7f : 16.7f;
    int base = bCast::VU_LIGHTS + c * 6;
    addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(x, 26)), module, base));
    for (int row = 1; row <= 2; ++row)
      addChild(createLightCentered<SmallLight<YellowLight>>(
        mm2px(Vec(x, 26 + 5 * row)), module, base + row
      ));
    for (int row = 3; row < 6; ++row)
      addChild(createLightCentered<SmallLight<GreenLight>>(
        mm2px(Vec(x, 26 + 5 * row)), module, base + row
      ));
  }

  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.7, 66)), module, bCast::LEVEL_PARAM));
  addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(12.7, 78)), module, bCast::PRESET_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 90)), module, bCast::LEFT_INPUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 106)), module, bCast::RIGHT_INPUT));
}

void bCastWidget::step() {
  ModuleWidget::step();
  bCast* module = getModule<bCast>();
  if (!module) return;
  // on the UI thread, like module removal, so claiming can't race it.
  // claim the engine clock while streaming if no audio module has it
  engine::Engine* engine = APP->engine;
  if (!engine->getMasterModule() && module->streaming.load()) module->setMaster(true);
  if (module->isMaster()) engine->setSuggestedSampleRate(bcast::SAMPLE_RATE);
}

void bCastWidget::appendContextMenu(Menu* menu) {
  bCast* module = getModule<bCast>();
  if (!module) return;

  menu->addChild(new MenuSeparator);
  menu->addChild(createBoolMenuItem("Master audio module", "",
    [=]() { return module->isMaster(); },
    [=](bool on) { module->setMaster(on); }
  ));
  menu->addChild(createBoolPtrMenuItem("DC blocker", "", &module->dcFilterEnabled));

  std::vector<std::string> labels;
  for (const bcast::Preset& p : bcast::PRESETS) labels.push_back(p.name);
  menu->addChild(createIndexSubmenuItem("Preset", labels,
    [=]() { return (size_t)module->preset(); },
    [=](size_t i) { module->setPreset((int)i); }
  ));
  menu->addChild(new MenuSeparator);

  if (module->isManaged()) {
    bCastSnapshot snap = module->snapshot();
    menu->addChild(createMenuLabel("Managed by OSCctrl"));
    menu->addChild(createMenuLabel(
      snap.config.enabled
        ? string::f("Streaming to %s:%d", snap.config.host.c_str(), snap.config.port)
        : "Not streaming"
    ));
    bCastClientStats s = module->clientStats();
    if (s.valid) {
      menu->addChild(createMenuLabel(string::f(
        "Client: %d underruns, %d lost, %d recovered", s.underruns, s.lost, s.recovered
      )));
      menu->addChild(createMenuLabel(string::f(
        "Jitter %.1f ms, buffer %.1f ms", s.jitterMs, s.bufferMs
      )));
    } else {
      menu->addChild(createMenuLabel("No client stats yet"));
    }
  } else {
    bCastConfig config = module->standaloneConfig();

    menu->addChild(createBoolMenuItem("Stream", "",
      [=]() { return module->standaloneConfig().enabled; },
      [=](bool on) { module->setStandalone([&](bCastConfig& c) { c.enabled = on; }); }
    ));

    menu->addChild(createMenuLabel("Destination host (Enter to apply)"));
    ConfigField* hostField = new ConfigField;
    hostField->setText(config.host);
    hostField->commit = [=](const std::string& text) {
      std::string host = string::trim(text);
      if (host.empty()) return;
      module->setStandalone([&](bCastConfig& c) { c.host = host; });
    };
    menu->addChild(hostField);

    menu->addChild(createMenuLabel("Destination port (Enter to apply)"));
    ConfigField* portField = new ConfigField;
    portField->setText(std::to_string(config.port));
    portField->commit = [=](const std::string& text) {
      char* end = nullptr;
      long port = std::strtol(text.c_str(), &end, 10);
      if (end == text.c_str() || port <= 0 || port > 65535) return;
      module->setStandalone([&](bCastConfig& c) { c.port = (int)port; });
    };
    menu->addChild(portField);
  }

  if (uint32_t dropped = module->ringOverflows.load())
    menu->addChild(createMenuLabel(string::f("Dropped %u frames (ring full)", dropped)));
}

Model* modelbCast = createModel<bCast, bCastWidget>("bCast");
