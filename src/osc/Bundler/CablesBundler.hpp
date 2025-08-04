#pragma once

#include "Bundler.hpp"

struct CablesBundler : Bundler {
  CablesBundler();
  CablesBundler(int64_t cableId, int32_t returnId);

  void bundleCable(int64_t cableId, int32_t returnId = -1);
};
