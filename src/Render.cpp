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
  int steps = 0;

	RenderWidget(Render* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Render.svg")));

		/* addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0))); */
		/* addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0))); */
		/* addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH))); */
		/* addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH))); */
	}

  void step() override {
    DEBUG("step %d", ++steps);
  }
};


Model* modelRender = createModel<Render, RenderWidget>("Render");
