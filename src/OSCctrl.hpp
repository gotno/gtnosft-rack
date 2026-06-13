#include "plugin.hpp"

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

struct OSCctrlWidget : ModuleWidget {

  OscSender* osctx = NULL;
  OscReceiver* oscrx = NULL;
  ChunkedManager* chunkman = NULL;
  SubscriptionManager* subman = NULL;

  OSCctrlWidget(OSCctrl* module);
  ~OSCctrlWidget();

  virtual void step() override;

  // enqueue actions that need to run on the render thread
  std::queue<Action> actionQueue;
  std::mutex actionMutex;
  void enqueueAction(Action action);
  void processActionQueue();
};
