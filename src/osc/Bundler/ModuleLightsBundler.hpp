#pragma once

#include "Bundler.hpp"

struct ModuleLightsBundler : Bundler {
  ModuleLightsBundler(
    const std::vector<int64_t>& moduleIds,
    std::function<void()> callback
  );
};
