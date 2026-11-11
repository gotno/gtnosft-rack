#pragma once

#include <cstdint>
#include <functional>

namespace rack::app {
struct ParamWidget;
}

namespace gtnosft::param_interaction {

enum class Action { Click, Press, Release };
using Completion = std::function<void(bool)>;

// ui thread only. at most one parameter is synthetically held.
// returns acceptance; completion runs after dispatch (after release for clicks).
bool apply(
  Action action,
  rack::app::ParamWidget* param,
  Completion completion = {}
);

void step();

// reuses a remote hold on this parameter, or runs an isolated temporary drag.
bool withDrag(
  rack::app::ParamWidget* param,
  const std::function<void()>& action
);

void clear();
void forgetModule(int64_t moduleId);

} // namespace gtnosft::param_interaction
