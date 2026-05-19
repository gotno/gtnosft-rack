#pragma once

#include "Bundler.hpp"

struct CablesBundler : Bundler {
  CablesBundler();

  void bundleCable(int64_t cableId);
};
