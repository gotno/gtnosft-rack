#pragma once

#include "rack.hpp"

// counts calls to step(), returning true every `milliseconds` (approximately)
struct StepDivider {
  StepDivider() {
    reset();
  }

  // returns true when the counter reaches `maxSteps` and wraps around
  // resets if `frameRateLimit` changes
  bool process() {
    if (++currentStep >= maxSteps) {
      currentStep = 0;
      return true;
    }

    if (lastFrameRateLimit != rack::settings::frameRateLimit) reset();

    return false;
  }

  void setMilliseconds(const uint32_t& ms) {
    milliseconds = ms;
    reset();
  }

private:
  uint32_t milliseconds{1000};
  uint8_t currentStep, maxSteps;
  float lastFrameRateLimit;

  void reset() {
    currentStep = 0;
    lastFrameRateLimit = rack::settings::frameRateLimit;
    maxSteps = lastFrameRateLimit * milliseconds * 1.001f;
  }
};
