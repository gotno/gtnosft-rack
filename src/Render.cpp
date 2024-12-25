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

#include "osc/EchoPacker.hpp"

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

  OscSender* osctx = NULL;
  OscReceiver* oscrx = NULL;

  RenderWidget(Render* module) {
    setModule(module);
    setPanel(createPanel(asset::plugin(pluginInstance, "res/Render.svg")));

    if (!module) return;
    osctx = new OscSender();
    oscrx = new OscReceiver();
  }

  ~RenderWidget() {
    delete osctx;
    delete oscrx;
  }

  struct ModuleWidgetContainer : widget::Widget {
    void draw(const DrawArgs& args) override {
      Widget::draw(args);
      Widget::drawLayer(args, 1);
    }
  };

  virtual void step() override {
    ModuleWidget::step();

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
    f.append(mw->getModule()->getModel()->name.c_str());
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

  // render FramebufferWidget to png
  void renderPng(std::string directory, std::string filename, rack::widget::FramebufferWidget* fb) {
    // auto start = std::chrono::high_resolution_clock::now();

    float zoom = 3.f;
    fb->render(math::Vec(zoom, zoom));

    nvgluBindFramebuffer(fb->getFramebuffer());
    int width, height;
    // nvgReset(APP->window->vg);
    nvgImageSize(APP->window->vg, fb->getImageHandle(), &width, &height);
    INFO("render png pixel size %dw/%dh: %d bytes", width, height, width * height * 4);
    // audio2 1,026,000
    uint8_t* pixels = new uint8_t[height * width * 4];
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    flipBitmap(pixels, width, height, 4);

    // Write pixels to PNG
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

    // auto end = std::chrono::high_resolution_clock::now();
    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // DEBUG("render png execution time: %lld microseconds", duration.count());
  }

  void renderDummyModule(rack::app::ModuleWidget* moduleWidget) {
    widget::FramebufferWidget* fb =
      wrapModuleWidget(
        makeDummyModuleWidget(moduleWidget)
      );

    renderPng("render_dummy", makeFilename(moduleWidget), fb);

    delete fb;
  }


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

  void renderDummyPanel(rack::app::ModuleWidget* moduleWidget) {
    rack::app::ModuleWidget* mw = makeDummyModuleWidget(moduleWidget);
    abandonChildren(mw);

    widget::FramebufferWidget* fb = wrapModuleWidget(mw);

    renderPng("render_dummy_panel", makeFilename(moduleWidget), fb);

    delete fb;
  }

  void renderSurrogateModule(rack::app::ModuleWidget* moduleWidget) {
    rack::app::ModuleWidget* surrogate = makeSurrogateModuleWidget(moduleWidget);
    widget::FramebufferWidget* fb = wrapModuleWidget(surrogate);

    renderPng("render_surrogate", makeFilename(moduleWidget), fb);

    // nullify the underlying module so it isn't destroyed with the surrogate
    surrogate->module = NULL;
    delete fb;
  }

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
    menu->addChild(createMenuItem("Echo", "", [=] {
      osctx->enqueueMessage(new EchoPacker("YOOO DUUDE"));
    }));

    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuItem("Refresh modules", "", [=]() {
      refreshModuleWidgets();
    }));

    menu->addChild(createMenuItem("Refresh framebuffers", "", [=]() {
      refreshFramebuffers();
    }));

    if (framebuffers.size() > 0) {
      menu->addChild(new rack::ui::MenuSeparator);
      menu->addChild(createSubmenuItem("render panel framebuffer", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::widget::FramebufferWidget*> pair : framebuffers) {
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
              renderFramebuffer(pair.first, pair.second);
            }));
          }
        }
      ));
    }

    if (moduleWidgets.size() > 0) {
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

      menu->addChild(createSubmenuItem("render dummy module, no children", "",
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
