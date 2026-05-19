#pragma once

#include "Bundler.hpp"

enum class CableAckType {
	Unknown,
	Add,
	Remove
};

struct CableAckBundler : Bundler {
  CableAckBundler(CableAckType type, int64_t returnId = -1);

  CableAckType type{CableAckType::Unknown};
  int64_t returnId{-1};
  std::string address;

  CableAckBundler* success(int64_t cableId);
  CableAckBundler* fail(int64_t cableId = -1);
};
