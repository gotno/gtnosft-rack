#pragma once

#include "Bundler.hpp"

struct ModuleStateBundler : Bundler {
  ModuleStateBundler(int64_t moduleId, rack::math::Rect ctrlBox);
};
