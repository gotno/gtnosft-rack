#pragma once

#include "Bundler.hpp"

struct ParamValueAckBundler : Bundler {
  ParamValueAckBundler(int64_t moduleId, int32_t paramId);

  int64_t moduleId;
  int32_t paramId;

  ParamValueAckBundler* success(float value);
  ParamValueAckBundler* fail();

  void addMessage(float value);
};
