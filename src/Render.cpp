#include "plugin.hpp"


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

      rack::widget::Widget* widget;
      rack::widget::FramebufferWidget* fb;

      if ((widget = dynamic_cast<rack::widget::Widget*>(mw->children.front()))) {
        if ((fb = dynamic_cast<rack::widget::FramebufferWidget*>(widget->children.front()))) {
          std::string key = "";
          key.append(mw->getModule()->getModel()->plugin->slug.c_str());
          key.append("/");
          key.append(mw->getModule()->getModel()->name.c_str());
          key.append("-panel");
          framebuffers.emplace(key, fb);
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

  void appendContextMenu(Menu* menu) override {
    menu->addChild(new MenuSeparator);
    menu->addChild(createMenuItem("Refresh framebuffers", "", [=]() {
      refreshFramebuffers();
    }));

    if (framebuffers.size() > 0) {
      menu->addChild(new rack::ui::MenuSeparator);
      menu->addChild(createSubmenuItem("framebuffers", "",
        [=](Menu* menu) {
          for (std::pair<std::string, rack::widget::FramebufferWidget*> pair : framebuffers) {
            menu->addChild(createMenuItem(pair.first.c_str(), "", [=]() {}));
          }
        }
      ));
    }
  }
};


Model* modelRender = createModel<Render, RenderWidget>("Render");
