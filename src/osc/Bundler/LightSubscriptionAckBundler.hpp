#pragma once

#include "Bundler.hpp"

struct LightSubscriptionAckBundler : Bundler {
  LightSubscriptionAckBundler(int64_t moduleId, bool success);
};
