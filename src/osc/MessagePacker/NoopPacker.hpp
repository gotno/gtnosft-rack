#include "MessagePacker.hpp"

struct NoopPacker : MessagePacker {
  bool isNoop() override { return true; }
};
