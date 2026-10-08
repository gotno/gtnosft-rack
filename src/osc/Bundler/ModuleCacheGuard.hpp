#pragma once

#include "rack.hpp"

// ::ensure attaches this guard to a ModuleWidget, providing a callback when
// it is destroyed to clear it from OSCctrl's caches (lights, params, overlays)
struct ModuleCacheGuard : rack::widget::Widget {
  int64_t moduleId;

  ModuleCacheGuard(int64_t _moduleId);
  ~ModuleCacheGuard() override;

  // attach a guard to moduleWidget unless it already has one
  static void ensure(rack::app::ModuleWidget* moduleWidget, int64_t moduleId);

  // clear all cached widget pointers
  static void clearAll();
};
