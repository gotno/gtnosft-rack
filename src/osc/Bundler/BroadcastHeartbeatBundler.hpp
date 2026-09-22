#pragma once

#include "Bundler.hpp"

struct BroadcastHeartbeatBundler : Bundler {
  BroadcastHeartbeatBundler(int64_t ctrlId);
};
