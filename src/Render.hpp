#include "plugin.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <utility>

class OscSender;
class OscReceiver;
class ChunkedManager;
class Render;

struct ModuleWidgetContainer : widget::Widget {
  void draw(const DrawArgs& args) override {
    Widget::draw(args);
    Widget::drawLayer(args, 1);
  }
};

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

struct RenderWidget : ModuleWidget {
  std::map<std::string, rack::app::ModuleWidget*> moduleWidgets;
  std::pair<int32_t, rack::app::ModuleWidget*> moduleWidgetToStream{0, NULL};

  OscSender* osctx = NULL;
  OscReceiver* oscrx = NULL;
  ChunkedManager* chunkman = NULL;

  RenderWidget(Render* module);
  ~RenderWidget();

  virtual void step() override;

  // enqueue actions that need to run on the render thread
  std::queue<Action> actionQueue;
  std::mutex actionMutex;
  void enqueueAction(Action action);
  void processActionQueue();

  // make a new ModuleWidget with no Module
  rack::app::ModuleWidget* makeDummyModuleWidget(rack::app::ModuleWidget* mw);

  // make a new ModuleWidget and give it the old ModuleWidget's Module
  rack::app::ModuleWidget* makeSurrogateModuleWidget(rack::app::ModuleWidget* mw);

  std::string makeFilename(rack::app::ModuleWidget* mw);

  // wrap ModuleWidget in container and framebuffer
  rack::widget::FramebufferWidget* wrapModuleWidget(rack::app::ModuleWidget* mw);

  // render FramebufferWidget to rgba pixel array
  uint8_t* renderPixels(rack::widget::FramebufferWidget* fb, int& width, int& height, float zoom = 3.f);

  // render FramebufferWidget to png
  void renderPng(std::string directory, std::string filename, rack::widget::FramebufferWidget* fb);

  // remove params/ports/lights from ModuleWidget
  void abandonChildren(rack::app::ModuleWidget* mw);

  // remove children->front() from ModuleWidget, which should be Internal->panel
  void abandonPanel(rack::app::ModuleWidget* mw);

  // render module without actual data, as in library preview, save
  void saveModulePreviewRender(rack::app::ModuleWidget* moduleWidget);

  // render only panel framebuffer, save
  void savePanelRender(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f);

  // render only panel framebuffer, compress & send
  void sendPanelRender(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f);

  // render only panel framebuffer, send
  void sendPanelRenderUncompressed(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f);

  // render module without panel or params/ports/lights, compress & send
  // TODO: probably more efficient to hold on to the surrogate and update its module each time
  int32_t sendOverlayRender(rack::app::ModuleWidget* moduleWidget, float zoom = 1.f);

  void sendModuleInfo(rack::app::ModuleWidget* moduleWidget);

  rack::widget::FramebufferWidget* getPanelFramebuffer(
    rack::app::ModuleWidget* moduleWidget
  );

  void refreshModuleWidgets();

  void logChildren(std::string& name, rack::app::ModuleWidget* mw);

  void flipBitmap(uint8_t* pixels, int width, int height, int depth);

  void appendContextMenu(Menu* menu) override;
};
