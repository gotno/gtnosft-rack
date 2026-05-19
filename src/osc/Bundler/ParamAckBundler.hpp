#pragma once

#include "Bundler.hpp"

struct ParamAckBundler : Bundler {
  ParamAckBundler(int64_t moduleId, int32_t paramId);

  int64_t moduleId;
  int32_t paramId;

  ParamAckBundler* success(float value);
  ParamAckBundler* fail();
};
