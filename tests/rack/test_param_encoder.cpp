#include <engine/Engine.hpp>
#include <history.hpp>
#undef PRIVATE
#include <rack.hpp>

#include "test.hpp"
#include "osc/ParamInteraction.hpp"
#include "osc/ParamWrite.hpp"

#include <memory>

namespace interaction = gtnosft::param_interaction;
namespace writes = gtnosft::param_write;

namespace {

// NoisePlethora's engine-side gesture rules, without its DSP/graphics dependencies.
struct EncoderModule : rack::engine::Module {
  bool held = false, dragged = false, bankMode = false;
  rack::dsp::BooleanTrigger trigger;
  rack::dsp::Timer timer;
  float reference = 0.f;
  int clicks = 0, modeChanges = 0, programSteps = 0;

  EncoderModule() {
    config(7, 0, 0, 0);
    configParam(6, -INFINITY, INFINITY, 0.f, "Program/Bank");
  }

  void process(const ProcessArgs& args) override {
    if (dragged) {
      int delta = int(8.f * (params[6].getValue() - reference));
      if (delta) {
        programSteps += delta;
        reference = params[6].getValue();
      }
    }
    if (trigger.process(held)) timer.reset();
    if (dragged) {
      timer.reset();
      held = false;
    } else if (held) {
      timer.process(args.sampleTime);
      if (timer.time > 0.5f) {
        held = false;
        timer.reset();
        bankMode = !bankMode;
        ++modeChanges;
      }
    } else if (timer.time > 0.f) {
      ++clicks;
      timer.reset();
    }
    if (!dragged) reference = params[6].getValue();
  }
};

struct EncoderKnob : rack::app::Knob {
  EncoderModule& state;
  rack::math::Vec displacement;
  int starts = 0, moves = 0, ends = 0;

  explicit EncoderKnob(EncoderModule& state) : state(state) {
    module = &state;
    paramId = 6;
    box.size = {20, 20};
  }

  void onButton(const ButtonEvent& e) override { e.consume(this); }

  void onDragStart(const DragStartEvent& e) override {
    ++starts;
    state.held = true;
    state.dragged = false;
    displacement = {};
    Knob::onDragStart(e);
  }

  void onDragMove(const DragMoveEvent& e) override {
    ++moves;
    displacement += e.mouseDelta;
    if (displacement.norm() > 5.f) {
      state.held = false;
      state.dragged = true;
    }
    // The adapter's activation is horizontal and assigns the exact value itself.
    // Skip Rack's OS modifier/cursor queries in this headless fixture.
  }

  void onDragEnd(const DragEndEvent& e) override {
    ++ends;
    state.held = false;
    Knob::onDragEnd(e);
  }
};

struct EncoderFixture {
  rack::Context context;
  rack::plugin::Plugin plugin;
  rack::plugin::Model model;
  EncoderModule engineModule;
  std::unique_ptr<rack::app::ModuleWidget> module;
  EncoderKnob* knob;
  rack::settings::KnobMode previousMode = rack::settings::knobMode;

  EncoderFixture() {
    rack::contextSet(&context);
    context.event = new rack::widget::EventState;
    context.engine = new rack::engine::Engine;
    context.history = new rack::history::State;
    context.engine->setSampleRate(1000.f);
    // Real Knob start/end handlers can run without an OS window in rotary mode.
    rack::settings::knobMode = rack::settings::KNOB_MODE_ROTARY_RELATIVE;
    plugin.slug = "Befaco";
    model.plugin = &plugin;
    model.slug = "NoisePlethora";
    engineModule.model = &model;
    context.engine->addModule(&engineModule);
    module = std::make_unique<rack::app::ModuleWidget>();
    knob = new EncoderKnob(engineModule);
    module->addParam(knob);
  }

  ~EncoderFixture() {
    interaction::clear();
    writes::clear();
    module.reset();
    context.engine->removeModule(&engineModule);
    rack::settings::knobMode = previousMode;
    rack::contextSet(nullptr);
  }
};

}

TEST(param_encoder_substep_updates_preserve_hold_timer_and_logical_value) {
  EncoderFixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.knob));
  CHECK(writes::applyOverride(f.knob, 0.04f) == writes::Result::Applied);
  CHECK(writes::applyOverride(f.knob, 0.09f) == writes::Result::Applied);
  CHECK_EQ(writes::getValue(f.knob), 0.09f);
  CHECK_EQ(f.knob->getParamQuantity()->getValue(), 0.f);
  CHECK_EQ(f.knob->starts, 1);
  CHECK_EQ(f.knob->moves, 0);
  CHECK_EQ(f.knob->ends, 0);
  f.context.engine->stepBlock(501);
  CHECK_EQ(f.engineModule.modeChanges, 1);
  CHECK(f.engineModule.bankMode);
  CHECK(interaction::apply(interaction::Action::Release, f.knob));
  CHECK(interaction::apply(interaction::Action::Press, f.knob));
  f.context.engine->stepBlock(2);
  interaction::step();
  CHECK(writes::applyOverride(f.knob, 0.09f) == writes::Result::Applied);
  CHECK_EQ(writes::getValue(f.knob), 0.09f);
}

TEST(param_encoder_real_steps_reuse_hold_and_cancel_long_press) {
  EncoderFixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.knob));
  CHECK(writes::applyOverride(f.knob, 0.13f) == writes::Result::Applied);
  CHECK_EQ(f.knob->starts, 1);
  CHECK_EQ(f.knob->moves, 1);
  CHECK_EQ(f.knob->ends, 0);
  CHECK_EQ(writes::getValue(f.knob), 0.13f);
  CHECK_EQ(f.knob->getParamQuantity()->getValue(), 0.125f);
  f.context.engine->stepBlock(600);
  CHECK_EQ(f.engineModule.programSteps, 1);
  CHECK_EQ(f.engineModule.modeChanges, 0);
  CHECK_EQ(f.engineModule.clicks, 0);
  CHECK(interaction::apply(interaction::Action::Release, f.knob));
  CHECK_EQ(f.knob->ends, 1);
}

TEST(param_encoder_temporary_writes_do_not_own_desktop_drag_state) {
  EncoderFixture f;
  CHECK(writes::applyOverride(f.knob, 0.13f) == writes::Result::Applied);
  CHECK_EQ(f.knob->starts, 1);
  CHECK_EQ(f.knob->ends, 1);
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  f.context.engine->stepBlock(2);
  CHECK_EQ(f.engineModule.programSteps, 1);
}

TEST(param_encoder_click_waits_for_engine_observation_before_completion) {
  int completions = 0;
  EncoderFixture f;
  CHECK(interaction::apply(interaction::Action::Click, f.knob, [&](bool ok) {
    CHECK(ok);
    CHECK(!f.engineModule.held);
    ++completions;
  }));
  interaction::step();
  CHECK_EQ(completions, 0);
  CHECK(f.engineModule.held);
  f.context.engine->stepBlock(1);
  interaction::step();
  CHECK_EQ(completions, 0);
  f.context.engine->stepBlock(1);
  interaction::step();
  CHECK_EQ(completions, 1);
  CHECK(!f.engineModule.held);
  f.context.engine->stepBlock(2);
  CHECK_EQ(f.engineModule.clicks, 1);
  CHECK_EQ(f.engineModule.modeChanges, 0);
}

TEST(param_encoder_queued_clicks_have_engine_visible_releases_between_them) {
  int completions = 0;
  EncoderFixture f;
  for (int i = 0; i < 2; ++i)
    CHECK(interaction::apply(interaction::Action::Click, f.knob, [&](bool ok) {
      CHECK(ok);
      ++completions;
    }));
  CHECK_EQ(f.knob->starts, 1);
  CHECK_EQ(completions, 0);
  for (int i = 0; i < 4; ++i) {
    f.context.engine->stepBlock(2);
    interaction::step();
  }
  CHECK_EQ(completions, 2);
  CHECK_EQ(f.engineModule.clicks, 2);
  CHECK_EQ(f.engineModule.modeChanges, 0);
}

TEST(param_encoder_immediate_press_release_is_not_lost_between_engine_samples) {
  int releases = 0;
  EncoderFixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.knob));
  CHECK(interaction::apply(interaction::Action::Release, f.knob, [&](bool ok) {
    CHECK(ok);
    ++releases;
  }));
  CHECK_EQ(releases, 0);
  CHECK(f.engineModule.held);
  f.context.engine->stepBlock(2);
  interaction::step();
  CHECK_EQ(releases, 1);
  f.context.engine->stepBlock(2);
  CHECK_EQ(f.engineModule.clicks, 1);
}

TEST(param_encoder_clearing_session_preserves_release_gap_before_a_new_hold) {
  EncoderFixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.knob));
  f.context.engine->stepBlock(300);
  interaction::clear();
  CHECK(interaction::apply(interaction::Action::Press, f.knob));
  CHECK_EQ(f.knob->starts, 1);
  f.context.engine->stepBlock(2);
  interaction::step();
  CHECK_EQ(f.knob->starts, 2);
  f.context.engine->stepBlock(201);
  CHECK_EQ(f.engineModule.modeChanges, 0);
  f.context.engine->stepBlock(300);
  CHECK_EQ(f.engineModule.modeChanges, 1);
}
