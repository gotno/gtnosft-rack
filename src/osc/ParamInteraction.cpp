#include "ParamInteraction.hpp"
#include "Bundler/ModuleCacheGuard.hpp"

#include <rack.hpp>
#include <deque>
#include <optional>
#include <utility>

namespace gtnosft::param_interaction {
namespace {

struct Target {
  rack::app::ModuleWidget* module;
  rack::app::ParamWidget* param;
  int64_t moduleId;
  int paramId;

  bool valid() const { return module->getParam(paramId) == param; }
};

struct EngineFence {
  rack::engine::Engine* engine;
  int64_t frame;

  static EngineFence now() { return {APP->engine, APP->engine->getFrame()}; }

  bool observed() const {
    // wait two engine steps to give the module time to use the altered state
    return APP && APP->engine == engine && engine->getFrame() - frame >= 2;
  }
};

struct ReleasedParam {
  Target target;
  EngineFence fence;
  bool waitingForReset;

  bool observed() {
    if (waitingForReset) {
      if (target.valid()) {
        auto* pq = target.param->getParamQuantity();
        if (pq && pq->getValue() != pq->getMinValue()) return false;
      }
      waitingForReset = false;
      fence.frame = fence.engine->getFrame();
    }
    return fence.observed();
  }
};

struct HeldParam {
  Target target;
  EngineFence pressed;
  bool click;
  Completion completion;
};

struct Request {
  Action action;
  Target target;
  Completion completion;
};

std::optional<HeldParam> held;
std::optional<ReleasedParam> released;
std::deque<Request> requests;
rack::widget::EventState synthetic;

struct ScopedCursorLock {
  bool previous = rack::settings::allowCursorLock;
  ScopedCursorLock() { rack::settings::allowCursorLock = false; }
  ~ScopedCursorLock() { rack::settings::allowCursorLock = previous; }
};

void complete(Completion completion, bool success) {
  if (completion) completion(success);
}

void completeFront(bool success) {
  auto completion = std::move(requests.front().completion);
  requests.pop_front();
  complete(std::move(completion), success);
}

void cancelAll(std::deque<Request>& canceled) {
  for (auto& request : canceled)
    complete(std::move(request.completion), false);
}

bool pointerBusy() {
  bool hasDraggedWidget = APP->event->getDraggedWidget();
  bool isCursorLocked = APP->window && APP->window->isCursorLocked();
  return hasDraggedWidget || isCursorLocked;
}

// dispatch in parameter-local coordinates to skip checks for on-screen, hidden, etc.
rack::widget::Widget* button(rack::app::ParamWidget* param, int action) {
  rack::widget::EventContext context;
  rack::widget::Widget::ButtonEvent event;
  event.context = &context;
  event.pos = param->box.size.div(2.f);
  event.button = GLFW_MOUSE_BUTTON_LEFT;
  event.action = action;
  event.mods = 0;
  param->onButton(event);
  return context.target;
}

void releaseHeld(bool success, bool dispatch = true) {
  auto previous = std::exchange(held, std::nullopt);
  if (!previous) return;

  if (dispatch) {
    ScopedCursorLock cursorLock;

    auto* target = button(previous->target.param, GLFW_RELEASE);
    synthetic.setDragHoveredWidget(nullptr);

    if (target && synthetic.getDraggedWidget()) {
      rack::widget::Widget::DragDropEvent event;
      event.button = GLFW_MOUSE_BUTTON_LEFT;
      event.origin = synthetic.getDraggedWidget();
      target->onDragDrop(event);
    }

    synthetic.setDraggedWidget(nullptr, 0);
    synthetic.setSelectedWidget(nullptr);

    auto* switchWidget =
      dynamic_cast<rack::app::Switch*>(previous->target.param);
    released = ReleasedParam{
      previous->target,
      EngineFence::now(),
      switchWidget && switchWidget->momentary
    };
  }

  synthetic = {};
  complete(std::move(previous->completion), success);
}

bool containsWidget(rack::widget::Widget* root, rack::widget::Widget* widget) {
  if (!widget || root == widget) return true;
  for (auto* child : root->children)
    if (containsWidget(child, widget)) return true;
  return false;
}

void synchronize() {
  if (!held) return;

  bool invalid =
    !APP ||
    APP->engine != held->pressed.engine ||
    !held->target.valid();

  if (!invalid) {
    auto* param = held->target.param;
    auto* native = APP->event ? APP->event->getDraggedWidget() : nullptr;
    invalid =
      !containsWidget(param, synthetic.getDraggedWidget()) ||
      !containsWidget(param, synthetic.getSelectedWidget()) ||
      (native && containsWidget(param, native));
  }

  if (invalid) {
    WARN("parameter interaction canceled: target changed or native pointer took over");
    releaseHeld(false, false);
  }
}

} // namespace

void step() {
  synchronize();
  if (!APP || !APP->event || !APP->engine) return;

  if (held && held->click && held->pressed.observed())
    releaseHeld(true);
  if (released && (released->fence.engine != APP->engine || released->observed()))
    released.reset();

  while (!requests.empty()) {
    auto& request = requests.front();
    if (!request.target.valid()) {
      completeFront(false);
      continue;
    }
    if (held && held->click) return;

    if (request.action == Action::Release) {
      if (held && held->target.param == request.target.param) {
        if (!held->pressed.observed()) return;
        releaseHeld(true);
      }
      completeFront(true);
      continue;
    }

    if (pointerBusy()) {
      WARN("parameter interaction failed: another pointer interaction is active");
      completeFront(false);
      continue;
    }
    if (held) {
      if (!held->pressed.observed()) return;
      releaseHeld(true);
    }
    if (released) {
      if (!released->observed()) return;
      released.reset();
    }

    auto next = std::move(request);
    requests.pop_front();

    ScopedCursorLock cursorLock;
    auto* target = button(next.target.param, GLFW_PRESS);
    synthetic.setDraggedWidget(target, GLFW_MOUSE_BUTTON_LEFT);
    synthetic.setSelectedWidget(target);

    held = HeldParam{
      next.target, EngineFence::now(),
      next.action == Action::Click, {}
    };
    if (held->click) {
      held->completion = std::move(next.completion);
    } else {
      complete(std::move(next.completion), true);
    }
  }
}

bool apply(Action action, rack::app::ParamWidget* param, Completion completion) {
  auto fail = [&](const char* reason) {
    WARN("parameter interaction failed: %s", reason);
    complete(std::move(completion), false);
    return false;
  };

  if (
    !param ||
    !param->module ||
    param->paramId < 0 ||
    static_cast<size_t>(param->paramId) >= param->module->paramQuantities.size() ||
    !param->getParamQuantity()
  ) return fail("parameter or quantity is unavailable");

  auto* module = param->getAncestorOfType<rack::app::ModuleWidget>();
  if (!module) return fail("parameter is not attached to a module widget");

  if (
    !APP ||
    !APP->event ||
    !APP->engine
  ) return fail("UI event state or engine is unavailable");

  if (action != Action::Release && pointerBusy())
    return fail("another pointer interaction is active");

  ModuleCacheGuard::ensure(module, param->module->id);
  requests.push_back({
    action,
    {module, param, param->module->id, param->paramId},
    std::move(completion)
  });

  step();

  return true;
}

bool withDrag(rack::app::ParamWidget* param, const std::function<void()>& action) {
  if (!APP || !APP->event || pointerBusy()) return false;

  synchronize();

  ScopedCursorLock cursorLock;
  if (held && held->target.param == param) {
    if (synthetic.getDraggedWidget() != param) return false;
    action();
    return true;
  }

  struct Drag {
    rack::widget::EventState events;
    ~Drag() { events.setDraggedWidget(nullptr, 0); }
  } drag;
  drag.events.setDraggedWidget(param, GLFW_MOUSE_BUTTON_LEFT);
  if (drag.events.getDraggedWidget() != param) return false;

  action();
  return true;
}

void clear() {
  auto pending = std::exchange(requests, {});
  synchronize();
  releaseHeld(false);
  cancelAll(pending);
}

void forgetModule(int64_t moduleId) {
  // the guard can run after parameter children have already been destroyed.
  std::deque<Request> canceled, kept;
  for (auto& request : requests) {
    auto& q = request.target.moduleId == moduleId ? canceled : kept;
    q.push_back(std::move(request));
  }

  requests = std::move(kept);

  if (held && held->target.moduleId == moduleId)
    releaseHeld(false, false);

  if (released && released->target.moduleId == moduleId)
    released.reset();

  cancelAll(canceled);
}

} // namespace gtnosft::param_interaction
