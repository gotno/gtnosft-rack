#include "plugin.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <chrono>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "osc/OscSender.hpp"
#include "osc/OscReceiver.hpp"
#include "osc/ChunkedManager.hpp"

#include "osc/MessagePacker/EchoPacker.hpp"
#include "osc/MessagePacker/BlobTestPacker.hpp"
#include "osc/MessagePacker/ModuleInfoPacker.hpp"
#include "osc/ChunkedSend/ChunkedTest.hpp"
#include "osc/ChunkedSend/ChunkedImage.hpp"

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

struct RenderWidget : ModuleWidget {
  std::map<std::string, rack::widget::FramebufferWidget*> framebuffers;
  std::map<std::string, rack::app::ModuleWidget*> moduleWidgets;

  rack::app::ModuleWidget* moduleWidgetToStream{NULL};
  OscSender* osctx = NULL;
  OscReceiver* oscrx = NULL;
  ChunkedManager* chunkman = NULL;

  RenderWidget(Render* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Render.svg")));

    if (!module) return;
    osctx = new OscSender();
    chunkman = new ChunkedManager(osctx);
    oscrx = new OscReceiver(chunkman);
  }

  ~RenderWidget() {
    delete osctx;
    delete oscrx;
    delete chunkman;
  }

  struct ModuleWidgetContainer : widget::Widget {
    void draw(const DrawArgs& args) override {
      Widget::draw(args);
      Widget::drawLayer(args, 1);
    }
  };

  virtual void step() override {
    ModuleWidget::step();

    if (moduleWidgetToStream) sendOverlayRender(moduleWidgetToStream);
  }

  // make a new ModuleWidget with no Module
  rack::app::ModuleWidget* makeDummyModuleWidget(rack::app::ModuleWidget* mw) {
    return mw->getModel()->createModuleWidget(NULL);
  }

  // make a new ModuleWidget and give it the old ModuleWidget's Module
  rack::app::ModuleWidget* makeSurrogateModuleWidget(rack::app::ModuleWidget* mw) {
    return mw->getModel()->createModuleWidget(mw->getModule());
  }

  std::string makeFilename(rack::app::ModuleWidget* mw) {
    std::string f = "";
    f.append(mw->getModule()->getModel()->plugin->slug.c_str());
    f.append("-");
    f.append(mw->getModule()->getModel()->slug.c_str());
    return f;
  }

  // wrap ModuleWidget in container and framebuffer
  rack::widget::FramebufferWidget* wrapModuleWidget(rack::app::ModuleWidget* mw) {
    widget::FramebufferWidget* fbcontainer = new widget::FramebufferWidget;
    ModuleWidgetContainer* mwcontainer = new ModuleWidgetContainer;

    fbcontainer->addChild(mwcontainer);
    mwcontainer->box.size = mw->box.size;
    fbcontainer->box.size = mw->box.size;
    mwcontainer->addChild(mw);

    return fbcontainer;
  }

  // render FramebufferWidget to rgba pixel array
  uint8_t* renderPixels(rack::widget::FramebufferWidget* fb, int& width, int& height, float zoom = 3.f) {
    fb->render(math::Vec(zoom, zoom));

    nvgluBindFramebuffer(fb->getFramebuffer());

    nvgImageSize(APP->window->vg, fb->getImageHandle(), &width, &height);

    uint8_t* pixels = new uint8_t[height * width * 4];
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    flipBitmap(pixels, width, height, 4);

    // audio2: 1026000 bytes / 1.026 megabytes
    // INFO("image size: %d bytes", width * height * 4);

    return pixels;
  }

  // render FramebufferWidget to png
  void renderPng(std::string directory, std::string filename, rack::widget::FramebufferWidget* fb) {
    // auto start = std::chrono::steady_clock::now();

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

  // render module without actual data, as in library
  void renderDummyModule(rack::app::ModuleWidget* moduleWidget) {
    widget::FramebufferWidget* fb =
      wrapModuleWidget(
        makeDummyModuleWidget(moduleWidget)
      );

    renderPng("render_dummy", makeFilename(moduleWidget), fb);

    delete fb;
  }


  // remove params/ports/lights
  void abandonChildren(rack::app::ModuleWidget* mw) {
    auto it = mw->children.begin();
    while (it != mw->children.end()) {
      if (dynamic_cast<rack::app::SvgScrew*>(*it) || dynamic_cast<rack::app::ParamWidget*>(*it) || dynamic_cast<rack::app::PortWidget*>(*it) || dynamic_cast<rack::app::LightWidget*>(*it)) {
        it = mw->children.erase(it);
      } else {
        ++it;
      }
    }
  }

  // remove children->front(), which should be Internal->panel
  void abandonPanel(rack::app::ModuleWidget* mw) {
    auto it = mw->children.begin();
    mw->children.erase(it);
  }

  // render panel with no params/ports/lights and without actual data
  void renderDummyPanel(rack::app::ModuleWidget* moduleWidget) {
    rack::app::ModuleWidget* mw = makeDummyModuleWidget(moduleWidget);
    abandonChildren(mw);

    widget::FramebufferWidget* fb = wrapModuleWidget(mw);

    renderPng("render_dummy_panel", makeFilename(moduleWidget), fb);

    delete fb;
  }

  // render surrogate ModuleWidget with actual module data, save png
  void renderSurrogateModule(rack::app::ModuleWidget* moduleWidget) {
    rack::app::ModuleWidget* surrogate = makeSurrogateModuleWidget(moduleWidget);
    widget::FramebufferWidget* fb = wrapModuleWidget(surrogate);

    renderPng("render_surrogate", makeFilename(moduleWidget), fb);

    // nullify the underlying module so it isn't destroyed with the surrogate
    surrogate->module = NULL;
    delete fb;
  }

  // render only panel framebuffer, save
  void savePanelRender(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f) {
    rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);
    renderPng("render_panel_framebuffer", makeFilename(moduleWidget), fb);
  }

  // render only panel framebuffer, compress & send
  void sendPanelRender(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f) {
    rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);

    int width, height;
    uint8_t* pixels = renderPixels(fb, width, height, zoom);

    ChunkedImage* chunked = new ChunkedImage(pixels, width, height);
    chunked->compress();
    chunkman->add(chunked);
  }

  // render only panel framebuffer, send
  void sendPanelRenderUncompressed(rack::app::ModuleWidget* moduleWidget, float zoom = 3.f) {
    rack::widget::FramebufferWidget* fb = getPanelFramebuffer(moduleWidget);

    int width, height;
    uint8_t* pixels = renderPixels(fb, width, height, zoom);
    chunkman->add(new ChunkedImage(pixels, width, height));
  }

  // render module without panel or params/ports/lights, compress & send
  // TODO: probably more efficient to hold on to the surrogate and update its module each time
  void sendOverlayRender(rack::app::ModuleWidget* moduleWidget, float zoom = 1.f) {
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
  }

  // render surrogate ModuleWidget with actual module data, send
  void sendSurrogateModuleRender(rack::app::ModuleWidget* moduleWidget) {
    rack::app::ModuleWidget* surrogate = makeSurrogateModuleWidget(moduleWidget);
    widget::FramebufferWidget* fb = wrapModuleWidget(surrogate);

    int width, height;
    uint8_t* pixels = renderPixels(fb, width, height, isOverlay ? 1.f : 3.f);

    ChunkedImage* chunked = new ChunkedImage(pixels, width, height);
    chunked->compress();
    chunkman->add(chunked);

    surrogate->module = NULL;
    delete fb;
  }

  void sendModuleInfo(rack::app::ModuleWidget* moduleWidget) {
    osctx->enqueueMessage(new ModuleInfoPacker(moduleWidget));
  }

  // render the ModuleWidget directly
  // TODO: this would be ideal, but why is it blank?
  void renderModuleDirect(rack::app::ModuleWidget* mw) {
    rack::widget::Widget* parent = mw->parent;
    parent->removeChild(mw);

    widget::FramebufferWidget* fb = wrapModuleWidget(mw);

    // correct size, but blank?
    renderPng("render_direct", makeFilename(mw), fb);

    // front() is ModuleWidgetContainer
    fb->children.front()->removeChild(mw);
    parent->addChild(mw);

    delete fb;
  }

  rack::widget::FramebufferWidget* getPanelFramebuffer(
    rack::app::ModuleWidget* moduleWidget
  ) {
    rack::widget::Widget* panel = moduleWidget->children.front();
    if (!panel) return NULL;

    rack::widget::FramebufferWidget* fb =
      dynamic_cast<rack::widget::FramebufferWidget*>(panel->children.front());
    if (!fb) return NULL;

    return fb;
  }

  void refreshFramebuffers() {
    framebuffers.clear();

    for (int64_t& moduleId: APP->engine->getModuleIds()) {
      rack::app::ModuleWidget* mw = APP->scene->rack->getModule(moduleId);

      rack::widget::Widget* panel;
      rack::widget::FramebufferWidget* fb;

      if ((panel = dynamic_cast<rack::widget::Widget*>(mw->children.front()))) {
        for (rack::widget::Widget* child : panel->children) {
          if ((fb = dynamic_cast<rack::widget::FramebufferWidget*>(child))) {

            std::string key = "";
            key.append(mw->getModule()->getModel()->plugin->slug.c_str());
            key.append("-");
            key.append(mw->getModule()->getModel()->name.c_str());
            framebuffers.emplace(key, fb);
            break;
          }
        }
      }
    }
  }

  void refreshModuleWidgets() {
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

  void logChildren(std::string& name, rack::app::ModuleWidget* mw) {
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

  void renderFramebuffer(std::string filename, rack::widget::FramebufferWidget* fb) {
    widget::FramebufferWidget* fbcontainer = new widget::FramebufferWidget;
    fbcontainer->children.push_back(fb);

    renderPng("render_framebuffer", filename, fbcontainer);

    fbcontainer->children.clear();
    delete fbcontainer;
  }

  static void flipBitmap(uint8_t* pixels, int width, int height, int depth) {
    for (int y = 0; y < height / 2; y++) {
      int flipY = height - y - 1;
      uint8_t tmp[width * depth];
      std::memcpy(tmp, &pixels[y * width * depth], width * depth);
      std::memcpy(&pixels[y * width * depth], &pixels[flipY * width * depth], width * depth);
      std::memcpy(&pixels[flipY * width * depth], tmp, width * depth);
    }
  }

  void appendContextMenu(Menu* menu) override {
    menu->addChild(new MenuSeparator);

    if (moduleWidgetToStream) {
      menu->addChild(createMenuItem("stop streaming", "", [=]() {
        moduleWidgetToStream = NULL;
      }));
    }

    menu->addChild(new MenuSeparator);

    menu->addChild(createMenuItem("Echo", "", [=] {
      osctx->enqueueMessage(new EchoPacker("YOOO DUUDE"));
    }));

    menu->addChild(createMenuItem("Blob test", "", [=] {
      osctx->enqueueMessage(new BlobTestPacker());
    }));

    menu->addChild(createMenuItem("Chunked send test", "", [=] {
      size_t size = 50;
      uint8_t* data = new uint8_t[size];
      for (uint8_t i = 0; i < size; i++) data[i] = i + 1;
      chunkman->add(new ChunkedTest(data, size));
    }));

    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuItem("Refresh modules", "", [=]() {
      refreshModuleWidgets();
    }));

    menu->addChild(createMenuItem("Refresh framebuffers", "", [=]() {
      refreshFramebuffers();
    }));

    if (moduleWidgets.size() > 0) {
      menu->addChild(new rack::ui::MenuSeparator);

      menu->addChild(createSubmenuItem("send panel", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              sendModuleInfo(moduleWidget);
              sendPanelRender(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("send panel uncompressed", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              sendModuleInfo(moduleWidget);
              sendPanelRenderUncompressed(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("send overlay", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              sendOverlayRender(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("save panel render", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              savePanelRender(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(new rack::ui::MenuSeparator);

      menu->addChild(createSubmenuItem("render dummy module", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              renderDummyModule(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("render dummy panel", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              renderDummyPanel(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(new rack::ui::MenuSeparator);
      menu->addChild(createSubmenuItem("live render surrogate module", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              renderSurrogateModule(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("send rendered surrogate module", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              sendModuleInfo(moduleWidget);
              sendSurrogateModuleRender(moduleWidget);
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("set streamed widget", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              sendModuleInfo(moduleWidget);
              sendPanelRender(moduleWidget);
              moduleWidgetToStream = moduleWidget;
            }));
          }
        }
      ));

      menu->addChild(createSubmenuItem("render direct (reparented)", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
            rack::app::ModuleWidget* moduleWidget = pair.second;
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              renderModuleDirect(moduleWidget);
            }));
          }
        }
      ));
    }
  }
};


Model* modelRender = createModel<Render, RenderWidget>("Render");
