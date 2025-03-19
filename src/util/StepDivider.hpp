#pragma once

#include "rack.hpp"

// counts calls to step(), returning true every `milliseconds` (approximately)
struct StepDivider {
  StepDivider(uint32_t _milliseconds): milliseconds(_milliseconds) {
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

private:
  uint32_t milliseconds;
  uint8_t currentStep, maxSteps;
  float lastFrameRateLimit;

  void reset() {
    currentStep = 0;
    lastFrameRateLimit = rack::settings::frameRateLimit;
    maxSteps = lastFrameRateLimit * milliseconds * 1.001f;
  }
};
