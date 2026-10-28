#include "plugin.hpp"
#include "bench/Bench.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <utility>

struct OSCctrl;
class OscSender;
class OscReceiver;
class ChunkedManager;
class SubscriptionManager;

typedef std::function<void(void)> Action;

struct SceneAction : rack::widget::Widget {
  static void Create(Action action) {
    SceneAction* sceneAction = new SceneAction(action);
    APP->scene->addChild(sceneAction);
  }

  Action action;

  SceneAction(Action a) : action(a) {}

  void step() override {
    action();
    requestDelete();
  }
};

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
    TX_LIGHT,
    HEARTBEAT_IN_LIGHT,
    HEARTBEAT_OUT_LIGHT,
    LIGHTS_LEN
  };

  bool broadcasting{true};

  rack::dsp::PulseGenerator txPulse;
  rack::dsp::PulseGenerator hbInPulse;
  rack::dsp::PulseGenerator hbOutPulse;

  rack::dsp::ClockDivider lightDivider;

  rack::dsp::SchmittTrigger bcastEchoThreshold;

  OSCctrl();
  void process(const ProcessArgs &args) override;
};

struct OSCctrlWidget : ModuleWidget {
  OscSender* osctx = NULL;
  OscReceiver* oscrx = NULL;
  ChunkedManager* chunkman = NULL;
  SubscriptionManager* subman = NULL;

  OSCctrlWidget(OSCctrl* module);
  ~OSCctrlWidget();

  virtual void step() override;
  void appendContextMenu(Menu* menu) override;

  // enqueue actions that need to run on the render thread.
  // tag must be a string literal; it names the action in bench reports
  struct QueuedAction {
    Action action;
    const char* tag;
    BENCH(bench::time_point enqueuedAt;)
  };
  std::queue<QueuedAction> actionQueue;
  std::mutex actionMutex;
  void enqueueAction(Action action, const char* tag = "other");
  void processActionQueue();

  // actions run in order until this much of a frame is used; the rest
  // carry over to the next frame (at least one action runs per frame)
  static constexpr std::chrono::milliseconds ACTION_BUDGET{8};
  // UI thread only: actions taken from actionQueue but not yet run
  std::deque<QueuedAction> pendingActions;
  BENCH(std::atomic<bool> actionBudgetEnabled{true};)
};
