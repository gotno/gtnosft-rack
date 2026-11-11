// The standalone harness owns an Engine; plugin code only uses Rack's public API.
#include <engine/Engine.hpp>
#undef PRIVATE
#include <rack.hpp>

#include "test.hpp"
#include "osc/ParamInteraction.hpp"

#include <memory>

namespace interaction = gtnosft::param_interaction;

namespace {

struct TestButton : rack::app::Switch {
  std::vector<std::string>& events;
  std::string name;
  rack::widget::Widget* buttonTarget = nullptr;
  bool cursorLockDuringStart = true;
  bool cursorLockDuringEnd = true;

  TestButton(std::vector<std::string>& events, std::string name)
    : events(events), name(std::move(name)) {
    momentary = true;
    box.size = {20.f, 20.f};
  }

  void onButton(const ButtonEvent& e) override {
    CHECK_EQ(e.button, GLFW_MOUSE_BUTTON_LEFT);
    CHECK_EQ(e.mods, 0);
    CHECK(e.pos.equals(box.size.div(2.f)));
    events.push_back(name + (e.action == GLFW_PRESS ? ":press" : ":release"));
    // Avoid ParamWidget's unrelated touchedParam bookkeeping in this headless test.
    e.consume(buttonTarget ? buttonTarget : this);
  }

  void onDragStart(const DragStartEvent& e) override {
    cursorLockDuringStart = rack::settings::allowCursorLock;
    events.push_back(name + ":start");
    Switch::onDragStart(e);
  }

  void onDragDrop(const DragDropEvent& e) override {
    CHECK(e.origin == this);
    events.push_back(name + ":drop");
  }

  void onDragEnd(const DragEndEvent& e) override {
    cursorLockDuringEnd = rack::settings::allowCursorLock;
    events.push_back(name + ":end");
    Switch::onDragEnd(e);
  }
};

struct ButtonModule : rack::engine::Module {
  int presses[2] = {};
  bool high[2] = {};

  void process(const ProcessArgs&) override {
    for (int i = 0; i < 2; ++i) {
      bool current = params[i].getValue() > 0.5f;
      if (current && !high[i]) ++presses[i];
      high[i] = current;
    }
  }
};

struct Fixture {
  rack::Context context;
  ButtonModule engineModule;
  std::vector<std::string> events;
  std::unique_ptr<rack::app::ModuleWidget> module;
  TestButton* a;
  TestButton* b;

  Fixture() {
    rack::contextSet(&context);
    context.event = new rack::widget::EventState;
    context.engine = new rack::engine::Engine;
    engineModule.id = 42;
    engineModule.config(2, 0, 0, 0);
    module = std::make_unique<rack::app::ModuleWidget>();
    auto add = [&](int id, const char* name) {
      engineModule.configParam(id, 0.f, 1.f, 0.f, name);
      auto* button = new TestButton(events, name);
      button->module = &engineModule;
      button->paramId = id;
      button->initParamQuantity();
      module->addParam(button);
      return button;
    };
    a = add(0, "a");
    b = add(1, "b");
    context.engine->addModule(&engineModule);
  }

  ~Fixture() {
    interaction::clear();
    module.reset();
    context.engine->removeModule(&engineModule);
    rack::contextSet(nullptr);
  }

  void advance() {
    context.engine->stepBlock(2);
    interaction::step();
  }

  void stepWidgets() {
    for (auto* param : {a, b}) {
      param->step();
      param->step();
    }
    interaction::step();
  }
};

}

TEST(param_interaction_click_dispatches_button_and_drag_lifecycle) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Click, f.a));
  CHECK((f.events == std::vector<std::string>{"a:press", "a:start"}));
  interaction::step();
  CHECK_EQ(f.events.size(), 2u);
  f.advance();
  CHECK((f.events == std::vector<std::string>{
    "a:press", "a:start", "a:release", "a:drop", "a:end"
  }));
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  // Rack preserves immediate momentary clicks for a UI frame before resetting.
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 1.f);
  f.a->step();
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 1.f);
  f.a->step();
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 0.f);
}

TEST(param_interaction_press_holds_until_matching_release) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  f.a->step();
  f.a->step();
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 1.f);
  CHECK(interaction::apply(interaction::Action::Release, f.b));
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  CHECK_EQ(f.events.size(), 2u);
  CHECK(interaction::apply(interaction::Action::Release, f.a));
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  f.a->step();
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 0.f);
  auto events = f.events;
  CHECK(interaction::apply(interaction::Action::Release, f.a));
  CHECK(f.events == events);
}

TEST(param_interaction_new_press_releases_old_target_first) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  CHECK(interaction::apply(interaction::Action::Press, f.b));
  f.stepWidgets();
  f.advance();
  CHECK((f.events == std::vector<std::string>{
    "a:press", "a:start", "a:release", "a:drop", "a:end", "b:press", "b:start"
  }));
  CHECK(f.context.event->getDraggedWidget() == nullptr);
}

TEST(param_interaction_repeated_press_releases_and_represses_same_target) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.stepWidgets();
  f.advance();
  CHECK((f.events == std::vector<std::string>{
    "a:press", "a:start", "a:release", "a:drop", "a:end", "a:press", "a:start"
  }));
}

TEST(param_interaction_click_releases_an_existing_hold) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  CHECK(interaction::apply(interaction::Action::Click, f.b));
  f.stepWidgets();
  f.advance();
  f.advance();
  CHECK((f.events == std::vector<std::string>{
    "a:press", "a:start", "a:release", "a:drop", "a:end",
    "b:press", "b:start", "b:release", "b:drop", "b:end"
  }));
  CHECK(f.context.event->getDraggedWidget() == nullptr);
}

TEST(param_interaction_never_steals_a_real_mouse_drag) {
  Fixture f;
  f.context.event->setDraggedWidget(f.b, GLFW_MOUSE_BUTTON_LEFT);
  f.events.clear();
  CHECK(!interaction::apply(interaction::Action::Press, f.a));
  CHECK(!interaction::apply(interaction::Action::Click, f.a));
  CHECK(interaction::apply(interaction::Action::Release, f.b));
  interaction::clear();
  CHECK(f.events.empty());
  CHECK(f.context.event->getDraggedWidget() == f.b);
}

TEST(param_interaction_invalid_target_does_not_interrupt_a_hold) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  CHECK(!interaction::apply(interaction::Action::Press, nullptr));
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  CHECK_EQ(f.events.size(), 2u);
}

TEST(param_interaction_release_does_not_end_an_unrelated_real_drag) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  f.context.event->setDraggedWidget(f.b, GLFW_MOUSE_BUTTON_LEFT);
  f.events.clear();
  CHECK(interaction::apply(interaction::Action::Release, f.a));
  CHECK((f.events == std::vector<std::string>{"a:release", "a:drop", "a:end"}));
  CHECK(f.context.event->getDraggedWidget() == f.b);
}

TEST(param_interaction_suppresses_cursor_locks_only_during_its_callbacks) {
  Fixture f;
  bool previous = rack::settings::allowCursorLock;
  rack::settings::allowCursorLock = true;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  CHECK(!f.a->cursorLockDuringStart);
  CHECK(rack::settings::allowCursorLock);
  CHECK(interaction::apply(interaction::Action::Release, f.a));
  CHECK(!f.a->cursorLockDuringEnd);
  CHECK(rack::settings::allowCursorLock);
  rack::settings::allowCursorLock = previous;
}

TEST(param_interaction_clear_releases_held_buttons) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  interaction::clear();
  CHECK(f.context.event->getDraggedWidget() == nullptr);
  CHECK((f.events == std::vector<std::string>{
    "a:press", "a:start", "a:release", "a:drop", "a:end"
  }));
  auto events = f.events;
  interaction::clear();
  CHECK(f.events == events);
}

TEST(param_interaction_destroyed_module_leaves_no_dangling_hold) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.module.reset();
  interaction::clear();
  CHECK(f.context.event->getDraggedWidget() == nullptr);
}

TEST(param_interaction_cancels_pending_click_completion_on_module_removal) {
  int completions = 0;
  bool success = true;
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Click, f.a, [&](bool ok) {
    ++completions;
    success = ok;
  }));
  CHECK_EQ(completions, 0);
  f.module.reset();
  CHECK_EQ(completions, 1);
  CHECK(!success);
  interaction::step();
  CHECK_EQ(completions, 1);
}

TEST(param_interaction_clear_cancels_active_and_queued_clicks_once) {
  int completions = 0;
  Fixture f;
  for (auto* param : {f.a, f.b})
    CHECK(interaction::apply(interaction::Action::Click, param, [&](bool ok) {
      CHECK(!ok);
      ++completions;
    }));
  interaction::clear();
  CHECK_EQ(completions, 2);
  f.advance();
  CHECK_EQ(completions, 2);
}

TEST(param_interaction_native_takeover_of_same_target_keeps_the_native_press) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Press, f.a));
  f.advance();
  f.context.event->setDraggedWidget(f.a, GLFW_MOUSE_BUTTON_LEFT);
  f.events.clear();
  CHECK(interaction::apply(interaction::Action::Release, f.a));
  CHECK(f.events.empty());
  f.a->step();
  f.a->step();
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 1.f);
  CHECK(f.context.event->getDraggedWidget() == f.a);
}

TEST(param_interaction_detached_drag_target_cancels_click_safely) {
  bool completed = false;
  bool success = true;
  Fixture f;
  auto child = std::make_unique<rack::widget::Widget>();
  f.a->addChild(child.get());
  f.a->buttonTarget = child.get();
  CHECK(interaction::apply(interaction::Action::Click, f.a, [&](bool ok) {
    completed = true;
    success = ok;
  }));
  f.a->removeChild(child.get());
  f.a->buttonTarget = nullptr;
  f.advance();
  CHECK(completed);
  CHECK(!success);
  CHECK((f.events == std::vector<std::string>{"a:press"}));
}

TEST(param_interaction_queued_momentary_clicks_wait_for_the_widgets_value_reset) {
  Fixture f;
  CHECK(interaction::apply(interaction::Action::Click, f.a));
  CHECK(interaction::apply(interaction::Action::Click, f.a));
  f.advance();
  f.advance();
  CHECK_EQ(f.engineModule.presses[0], 1);
  CHECK_EQ(f.events.size(), 5u);
  f.a->step();
  f.a->step();
  CHECK_EQ(f.a->getParamQuantity()->getValue(), 0.f);
  interaction::step();
  f.advance();
  f.advance();
  CHECK_EQ(f.engineModule.presses[0], 2);
}
