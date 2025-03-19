#include "Render.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "osc/OscSender.hpp"
#include "osc/OscReceiver.hpp"
#include "osc/ChunkedManager.hpp"

#include "osc/MessagePacker/ModuleInfoPacker.hpp"
#include "osc/ChunkedSend/ChunkedImage.hpp"
#include "osc/MessagePacker/BasicHeartbeatPacker.hpp"

struct Render : Module {
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

  Render() {
    config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
  }

  void process(const ProcessArgs& args) override {
  }
};

RenderWidget::RenderWidget(Render* module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/Render.svg")));
  if (!module) return;

  osctx = new OscSender();
  chunkman = new ChunkedManager(osctx);
  oscrx = new OscReceiver(this, osctx, chunkman);

  heartbeatDivider.setMilliseconds(1000);
}

RenderWidget::~RenderWidget() {
  delete osctx;
  delete oscrx;
  delete chunkman;
}

void RenderWidget::step() {
  ModuleWidget::step();
  if (!module) return;

  if (moduleWidgetToStream.second && !chunkman->isProcessing(moduleWidgetToStream.first)) {
    moduleWidgetToStream.first = sendOverlayRender(moduleWidgetToStream.second, 2.f);
  }

  processActionQueue();


  if (heartbeatDivider.process())
    osctx->enqueueMessage(new BasicHeartbeatPacker());
}

void RenderWidget::enqueueAction(Action action) {
  std::lock_guard<std::mutex> locker(actionMutex);
  actionQueue.push(action);
}

void RenderWidget::processActionQueue() {
  std::lock_guard<std::mutex> locker(actionMutex);
  while (!actionQueue.empty()) {
    auto action = actionQueue.front();
    action();
    actionQueue.pop();
  }
}

rack::app::ModuleWidget* RenderWidget::makeDummyModuleWidget(
  rack::app::ModuleWidget* mw
) {
  return mw->getModel()->createModuleWidget(NULL);
}

// make a new ModuleWidget and give it the old ModuleWidget's Module
rack::app::ModuleWidget* RenderWidget::makeSurrogateModuleWidget(
  rack::app::ModuleWidget* mw
) {
  return mw->getModel()->createModuleWidget(mw->getModule());
}

std::string RenderWidget::makeFilename(rack::app::ModuleWidget* mw) {
  std::string f = "";
  f.append(mw->getModule()->getModel()->plugin->slug.c_str());
  f.append("-");
  f.append(mw->getModule()->getModel()->slug.c_str());
  return f;
}

// wrap ModuleWidget in container and framebuffer
rack::widget::FramebufferWidget* RenderWidget::wrapModuleWidget(
  rack::app::ModuleWidget* mw
) {
  widget::FramebufferWidget* fbcontainer = new widget::FramebufferWidget;
  ModuleWidgetContainer* mwcontainer = new ModuleWidgetContainer;

  fbcontainer->addChild(mwcontainer);
  mwcontainer->box.size = mw->box.size;
  fbcontainer->box.size = mw->box.size;
  mwcontainer->addChild(mw);

  return fbcontainer;
}

// render FramebufferWidget to rgba pixel array
uint8_t* RenderWidget::renderPixels(
  rack::widget::FramebufferWidget* fb,
  int& width,
  int& height,
  float zoom
) {
  fb->render(math::Vec(zoom, zoom));

  nvgluBindFramebuffer(fb->getFramebuffer());

  nvgImageSize(APP->window->vg, fb->getImageHandle(), &width, &height);

  uint8_t* pixels = new uint8_t[height * width * 4];
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
  flipBitmap(pixels, width, height, 4);

  return pixels;
}

// render FramebufferWidget to png
void RenderWidget::renderPng(
  std::string directory,
  std::string filename,
  rack::widget::FramebufferWidget* fb
) {
  int width, height;
  uint8_t* pixels = renderPixels(fb, width, height);

  std::string renderPath = rack::asset::user(directory);
  rack::system::createDirectory(renderPath);
  std::string filepath = rack::system::join(renderPath, filename + ".png");
  stbi_write_png(
    filepath.c_str(),
    width,
    height,
    4,
    pixels,
    width * 4
  );

  delete[] pixels;
  nvgluBindFramebuffer(NULL);
}

// remove params/ports/lights from ModuleWidget
void RenderWidget::abandonChildren(rack::app::ModuleWidget* mw) {
  auto it = mw->children.begin();
  while (it != mw->children.end()) {
    if (dynamic_cast<rack::app::SvgScrew*>(*it) || dynamic_cast<rack::app::ParamWidget*>(*it) || dynamic_cast<rack::app::PortWidget*>(*it) || dynamic_cast<rack::app::LightWidget*>(*it)) {
      it = mw->children.erase(it);
    } else {
      ++it;
    }
  }
}

// remove children->front() from ModuleWidget, which should be Internal->panel
void RenderWidget::abandonPanel(rack::app::ModuleWidget* mw) {
  auto it = mw->children.begin();
  mw->children.erase(it);
}

// render module without actual data, as in library preview, save
void RenderWidget::saveModulePreviewRender(
  rack::app::ModuleWidget* moduleWidget
) {
  widget::FramebufferWidget* fb =
    wrapModuleWidget(
      makeDummyModuleWidget(moduleWidget)
    );

  renderPng("render_module_preview", makeFilename(moduleWidget), fb);

  delete fb;
}

// render only panel framebuffer, save
void RenderWidget::savePanelRender(
  rack::app::ModuleWidget* moduleWidget,
  float zoom
) {
  rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);
  renderPng("render_panel_framebuffer", makeFilename(moduleWidget), fb);
}

// render only panel framebuffer, compress & send
void RenderWidget::sendPanelRender(
  rack::app::ModuleWidget* moduleWidget,
  float zoom
) {
  rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);

  int width, height;
  uint8_t* pixels = renderPixels(fb, width, height, zoom);

  ChunkedImage* chunked = new ChunkedImage(pixels, width, height);
  chunked->compress();
  chunkman->add(chunked);
}

// render only panel framebuffer, send
void RenderWidget::sendPanelRenderUncompressed(
  rack::app::ModuleWidget* moduleWidget,
  float zoom
) {
  rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);

  int width, height;
  uint8_t* pixels = renderPixels(fb, width, height, zoom);
  chunkman->add(new ChunkedImage(pixels, width, height));
}

// render module without panel or params/ports/lights, compress & send
// TODO: probably more efficient to hold on to the surrogate and update its module each time
int32_t RenderWidget::sendOverlayRender(
  rack::app::ModuleWidget* moduleWidget,
  float zoom
) {
  rack::app::ModuleWidget* surrogate = makeSurrogateModuleWidget(moduleWidget);
  widget::FramebufferWidget* fb = wrapModuleWidget(surrogate);
  abandonChildren(surrogate);
  abandonPanel(surrogate);

  int width, height;
  uint8_t* pixels = renderPixels(fb, width, height, zoom);

  ChunkedImage* chunked = new ChunkedImage(pixels, width, height);
  chunked->compress();
  chunked->isOverlay = true;
  chunkman->add(chunked);

  surrogate->module = NULL;
  delete fb;

  return chunked->id;
}

void RenderWidget::sendModuleInfo(rack::app::ModuleWidget* moduleWidget) {
  osctx->enqueueMessage(new ModuleInfoPacker(moduleWidget));
}

rack::widget::FramebufferWidget* RenderWidget::getPanelFramebuffer(
  rack::app::ModuleWidget* moduleWidget
) {
  rack::widget::Widget* panel = moduleWidget->children.front();
  if (!panel) return NULL;

  rack::widget::FramebufferWidget* fb =
    dynamic_cast<rack::widget::FramebufferWidget*>(panel->children.front());
  if (!fb) return NULL;

  return fb;
}

void RenderWidget::refreshModuleWidgets() {
  moduleWidgets.clear();

  for (int64_t& moduleId: APP->engine->getModuleIds()) {
    rack::app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);

    std::string key = "";
    key.append(mw->getModule()->getModel()->plugin->slug.c_str());
    key.append("-");
    key.append(mw->getModule()->getModel()->name.c_str());

    moduleWidgets.emplace(key, mw);
  }
}

void RenderWidget::logChildren(std::string& name, rack::app::ModuleWidget* mw) {
  DEBUG("\n\n%s", name.c_str());
  for (rack::widget::Widget* widget : mw->children) {
    DEBUG(
      "size %fx/%fy, pos %fx/%fy",
      widget->getSize().x,
      widget->getSize().y,
      widget->getPosition().x,
      widget->getPosition().y
    );

    if (dynamic_cast<rack::app::SvgButton*>(widget))
      DEBUG("  is SvgButton");
    if (dynamic_cast<rack::app::SvgKnob*>(widget))
      DEBUG("  is SvgKnob");
    if (dynamic_cast<rack::app::SvgPanel*>(widget))
      DEBUG("  is SvgPanel");
    if (dynamic_cast<rack::app::SvgPort*>(widget))
      DEBUG("  is SvgPort");
    if (dynamic_cast<rack::app::SvgScrew*>(widget))
      DEBUG("  is SvgScrew");
    if (dynamic_cast<rack::app::SvgSlider*>(widget))
      DEBUG("  is SvgSlider");
    if (dynamic_cast<rack::app::SvgSwitch*>(widget))
      DEBUG("  is SvgSwitch");

    if (dynamic_cast<rack::app::SliderKnob*>(widget))
      DEBUG("  is SliderKnob");
    if (dynamic_cast<rack::app::Knob*>(widget))
      DEBUG("  is Knob");

    if (dynamic_cast<rack::app::Switch*>(widget))
      DEBUG("  is Switch");

    if (dynamic_cast<rack::app::LedDisplay*>(widget))
      DEBUG("  is LedDisplay");

    if (dynamic_cast<rack::app::ModuleLightWidget*>(widget))
      DEBUG("  is ModuleLightWidget");
    if (dynamic_cast<rack::app::MultiLightWidget*>(widget))
      DEBUG("  is MultiLightWidget");
    if (dynamic_cast<rack::app::LightWidget*>(widget))
      DEBUG("  is LightWidget");

    if (dynamic_cast<rack::app::ParamWidget*>(widget))
      DEBUG("  is ParamWidget");
    if (dynamic_cast<rack::app::PortWidget*>(widget))
      DEBUG("  is PortWidget");
  }
}

void RenderWidget::flipBitmap(uint8_t* pixels, int width, int height, int depth) {
  for (int y = 0; y < height / 2; y++) {
    int flipY = height - y - 1;
    uint8_t tmp[width * depth];
    std::memcpy(tmp, &pixels[y * width * depth], width * depth);
    std::memcpy(&pixels[y * width * depth], &pixels[flipY * width * depth], width * depth);
    std::memcpy(&pixels[flipY * width * depth], tmp, width * depth);
  }
}

void RenderWidget::appendContextMenu(Menu* menu) {
  menu->addChild(new MenuSeparator);

  menu->addChild(createMenuItem("refresh modules", "", [=, this]() {
    refreshModuleWidgets();
  }));

  if (moduleWidgets.size() > 0) {
    menu->addChild(new rack::ui::MenuSeparator);

    if (moduleWidgetToStream.second) {
      menu->addChild(createMenuItem("stop streaming", "", [=, this]() {
        moduleWidgetToStream.second = NULL;
      }));
    }

    menu->addChild(createSubmenuItem("set streamed widget", "",
      [=, this](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          rack::app::ModuleWidget* moduleWidget = pair.second;
          menu->addChild(createMenuItem(pair.first.c_str(), "", [=, this]() {
            sendModuleInfo(moduleWidget);
            sendPanelRender(moduleWidget);
            moduleWidgetToStream.second = moduleWidget;
          }));
        }
      }
    ));

    menu->addChild(createSubmenuItem("send panel", "",
      [=, this](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          rack::app::ModuleWidget* moduleWidget = pair.second;
          menu->addChild(createMenuItem(pair.first.c_str(), "", [=, this]() {
            sendModuleInfo(moduleWidget);
            sendPanelRender(moduleWidget);
          }));
        }
      }
    ));

    menu->addChild(createSubmenuItem("send panel uncompressed", "",
      [=, this](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          rack::app::ModuleWidget* moduleWidget = pair.second;
          menu->addChild(createMenuItem(pair.first.c_str(), "", [=, this]() {
            sendModuleInfo(moduleWidget);
            sendPanelRenderUncompressed(moduleWidget);
          }));
        }
      }
    ));

    menu->addChild(createSubmenuItem("send overlay", "",
      [=, this](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          rack::app::ModuleWidget* moduleWidget = pair.second;
          menu->addChild(createMenuItem(pair.first.c_str(), "", [=, this]() {
            sendOverlayRender(moduleWidget, /* zoom */ 3.f);
          }));
        }
      }
    ));

    menu->addChild(createSubmenuItem("save panel render", "",
      [=, this](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          rack::app::ModuleWidget* moduleWidget = pair.second;
          menu->addChild(createMenuItem(pair.first.c_str(), "", [=, this]() {
            savePanelRender(moduleWidget);
          }));
        }
      }
    ));

    menu->addChild(createSubmenuItem("save module preview render", "",
      [=, this](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          rack::app::ModuleWidget* moduleWidget = pair.second;
          menu->addChild(createMenuItem(pair.first.c_str(), "", [=, this]() {
            saveModulePreviewRender(moduleWidget);
          }));
        }
      }
    ));
  }
}

Model* modelRender = createModel<Render, RenderWidget>("Render");
