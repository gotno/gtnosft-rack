#pragma once

#include "Bundler.hpp"

struct ParamActionAckBundler : Bundler {
  ParamActionAckBundler(
    int64_t moduleId,
    int32_t paramId,
    const std::string& action = "click"
  );

  int64_t moduleId;
  int32_t paramId;
  std::string action;

  ParamActionAckBundler* success();
  ParamActionAckBundler* fail();

  void addMessage(bool success);
};
