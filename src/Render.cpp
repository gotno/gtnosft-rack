#include "plugin.hpp"
#include "stb_image_write.h"


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

	RenderWidget(Render* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Render.svg")));

		/* addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0))); */
		/* addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0))); */
		/* addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH))); */
		/* addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH))); */
	}

  virtual void step() override {
    ModuleWidget::step();
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
            key.append("/");
            key.append(mw->getModule()->getModel()->name.c_str());
            key.append("-panel");
            DEBUG(
              "%s/%s %fx%f",
              mw->getModule()->getModel()->plugin->slug.c_str(),
              mw->getModule()->getModel()->name.c_str(),
              panel->getBox().size.x,
              panel->getBox().size.y
            );
            framebuffers.emplace(key, fb);
            break;
          }
        }
      }
      // if (panel) {
      // } else if (widget) {
      //   std::string key = "";
      //   key.append(mw->getModule()->getModel()->plugin->slug.c_str());
      //   key.append("/");
      //   key.append(mw->getModule()->getModel()->name.c_str());
      //   key.append("-panel (Widget)");
      // }
    }
  }

  void collectModuleWidgets() {
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

  void renderModuleWidget(std::string filename, rack::app::ModuleWidget* moduleWidget) {
    std::string renderPath = rack::asset::user("render_test");
    rack::system::createDirectory(renderPath);
    filename.append(".png");
    std::string filepath = rack::system::join(renderPath, filename.c_str());

    // Create widgets
    widget::FramebufferWidget* fbw = new widget::FramebufferWidget;

    // why does this work for `screenshotModules`, but not this?
    // fbw->oversample = 2;

    struct ModuleWidgetContainer : widget::Widget {
      void draw(const DrawArgs& args) override {
        Widget::draw(args);
        Widget::drawLayer(args, 1);
      }
    };
    ModuleWidgetContainer* mwc = new ModuleWidgetContainer;
    fbw->addChild(mwc);

    rack::app::ModuleWidget* mw = moduleWidget->getModel()->createModuleWidget(NULL);
    // rack::widget::Widget* parent = moduleWidget->parent;
    // parent->removeChild(moduleWidget);
    // rack::app::ModuleWidget* mw = moduleWidget;

    logChildren(moduleWidget->getModule()->getModel()->name, mw);

    // abandon children
    std::list<rack::widget::Widget*> abandonedChildren;
    auto it = mw->children.begin();
    while (it != mw->children.end()) {
      if (dynamic_cast<rack::app::ParamWidget*>(*it) || dynamic_cast<rack::app::PortWidget*>(*it) || dynamic_cast<rack::app::LightWidget*>(*it)) {
        abandonedChildren.push_back(*it);
        it = mw->children.erase(it);
        // (*it)->setVisible(false);
      } else {
        ++it;
      }
    }

    logChildren(moduleWidget->getModule()->getModel()->name, mw);

    mwc->box.size = mw->box.size;
    fbw->box.size = mw->box.size;
    mwc->addChild(mw);

    // Step to allow the ModuleWidget state to set its default appearance.
    fbw->step();

    // Draw to framebuffer
    float zoom = 3.f;
    fbw->render(math::Vec(zoom, zoom));

    // adopt children
    // auto it2 = abandonedChildren.begin();
    // auto it3 = mw->children.begin();
    // while (it2 != abandonedChildren.end()) {
    //   mw->children.insert(it3, *it2);
    //   ++it2;
    //   ++it3;
    // }

    logChildren(moduleWidget->getModule()->getModel()->name, mw);

    // Read pixels
    nvgluBindFramebuffer(fbw->getFramebuffer());
    int width, height;
    nvgReset(APP->window->vg);
    nvgImageSize(APP->window->vg, fbw->getImageHandle(), &width, &height);
    uint8_t* pixels = new uint8_t[height * width * 4];
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    // Write pixels to PNG
    flipBitmap(pixels, width, height, 4);
    stbi_write_png(filepath.c_str(), width, height, 4, pixels, width * 4);

    // Cleanup
    // mw->parent = parent;
    delete[] pixels;
    nvgluBindFramebuffer(NULL);
    delete fbw;
    // delete mwc;
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
    // menu->addChild(createMenuItem("Refresh framebuffers", "", [=]() {
    //   refreshFramebuffers();
    // }));

    // if (framebuffers.size() > 0) {
    //   menu->addChild(new rack::ui::MenuSeparator);
    //   menu->addChild(createSubmenuItem("framebuffers", "",
    //     [=](Menu* menu) {
    //       for (std::pair<std::string, rack::widget::FramebufferWidget*> pair : framebuffers) {
    //         rack::widget::FramebufferWidget* fb = pair.second;
    //         menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {
    //           GLint w = fb->getFramebufferSize().x;
    //           GLint h = fb->getFramebufferSize().y;
    //           nvgluBindFramebuffer(fb->getFramebuffer());
    //           unsigned char* pixels = (unsigned char*)malloc(w * h * 4);
    //           glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels); 
    //           nvgluBindFramebuffer(NULL);

    //           std::string dir = rack::asset::user("fb_test");
    //           rack::system::createDirectory(dir);
    //           stbi_write_png(rack::system::join(dir, "test.png").c_str(), w, h, 4, pixels, w * 4);
    //           free(pixels);
    //         }));
    //       }
    //     }
    //   ));
    // }

    menu->addChild(createMenuItem("Refresh modules", "", [=]() {
      collectModuleWidgets();
    }));

    if (moduleWidgets.size() > 0) {
      menu->addChild(new rack::ui::MenuSeparator);
      menu->addChild(createSubmenuItem("Modules", "", [=](Menu* menu) {
        for (std::pair<std::string, rack::app::ModuleWidget*> pair : moduleWidgets) {
          std::string& key = pair.first;
          rack::app::ModuleWidget* mw = pair.second;

          menu->addChild(createMenuItem(key.c_str(), "", [=]() {
            renderModuleWidget(key, mw);
          }));
        }
      }));
    }
  }
};


Model* modelRender = createModel<Render, RenderWidget>("Render");
