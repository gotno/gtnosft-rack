#pragma once

#include "Bundler.hpp"

struct ModuleStubsBundler : Bundler {
  ModuleStubsBundler();

  void addModule(int64_t id, std::string& pluginSlug, std::string& moduleSlug);
};
