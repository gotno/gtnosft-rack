#pragma once
#include "plugin.hpp"
#include "audio/Packet.hpp"
#include "audio/Presets.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace bcast { class AudioStreamer; class EngineClock; }

struct bCastConfig {
  bool enabled = false;
  std::string host = "127.0.0.1";
  int port = bcast::DEFAULT_PORT;
  int preset = bcast::DEFAULT_PRESET;

  bool operator==(const bCastConfig&) const = default;
};

// the client's latest /audio/stats, cumulative counts
struct bCastClientStats {
  int underruns = 0;
  int lost = 0;
  int recovered = 0;
  float jitterMs = 0.f;
  float bufferMs = 0.f;
  bool valid = false;
};

struct bCastSnapshot {
  bCastConfig config;
  bool managed = false;
  uint64_t version = 0;
};

struct bCast : Module {
  enum ParamId { LEVEL_PARAM, PRESET_PARAM, PARAMS_LEN };
  enum InputId { LEFT_INPUT, RIGHT_INPUT, INPUTS_LEN };
  enum OutputId { OUTPUTS_LEN };
  enum LightId { ENUMS(STATUS_LIGHT, 3), ENUMS(VU_LIGHTS, 12), LIGHTS_LEN };

  // engine thread -> streamer thread (single producer, single consumer).
  // 16384 frames is 340 ms at 48 kHz, 85 ms at 192 kHz.
  dsp::RingBuffer<dsp::Frame<2>, 16384> ring;
  // set by the streamer; process() only pushes while it's true
  std::atomic<bool> streaming{false};
  std::atomic<float> engineSampleRate{0.f};
  std::atomic<uint32_t> ringOverflows{0};
  std::atomic<uint16_t> epoch{0};
  std::atomic<bool> sendFailing{false};
  bool dcFilterEnabled = true;

  bCast();
  ~bCast() override;

  void process(const ProcessArgs& args) override;
  void onAdd(const AddEvent& e) override;
  void onRemove(const RemoveEvent& e) override;
  void onSampleRateChange(const SampleRateChangeEvent& e) override;
  json_t* dataToJson() override;
  void dataFromJson(json_t* rootJ) override;

  uint32_t streamId() const { return (uint32_t)id; }

  // config API, any thread. version bumps whenever the effective config
  // (standalone, or managed while attached to OSCctrl) changes
  bCastSnapshot snapshot();
  bool isManaged() const { return managed.load(); }
  bCastConfig standaloneConfig();
  void setStandalone(const std::function<void(bCastConfig&)>& edit);
  // attaching starts managed mode disabled; the client enables it
  void setManaged(bool on);
  void setManagedEnabled(bool enabled, const std::string& host, int port);
  void setManagedPreset(int preset);
  // the preset is shared by both modes; the knob, menu and client all set it
  int preset() const { return presetCache.load(); }
  void setPreset(int preset);
  void setClientStats(const bCastClientStats& stats);
  bCastClientStats clientStats();

  // engine master module: bCast clocks the engine in real time while it's master
  bool isMaster();
  void setMaster(bool on);

private:
  dsp::RCFilter dcFilters[2];
  dsp::VuMeter2 vuMeters[2];
  dsp::ClockDivider lightDivider;

  std::mutex configMutex;
  bCastConfig standalone;
  bCastConfig managedConfig;
  std::atomic<bool> managed{false};  // written under configMutex
  uint64_t version = 0;
  bCastClientStats stats;
  std::atomic<int> presetCache{bcast::DEFAULT_PRESET};
  int lastPresetKnob = -1;  // engine thread

  std::unique_ptr<bcast::AudioStreamer> streamer;
  std::unique_ptr<bcast::EngineClock> engineClock;

  const bCastConfig& effective() const { return managed ? managedConfig : standalone; }
  // runs change under the lock and bumps version if the effective config moved
  void update(const std::function<void()>& change);
  void stopStreamer();
  void syncPresetKnob();
};

struct bCastWidget : ModuleWidget {
  bCastWidget(bCast* module);
  void step() override;
  void appendContextMenu(Menu* menu) override;
};
