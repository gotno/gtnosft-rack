#pragma once

#include "../../bench/Bench.hpp"

#ifdef GTNOSFT_BENCH

#include "Bundler.hpp"

// replies to /bench/report with a snapshot of the benchmark stats
struct BenchReportBundler : Bundler {
  BenchReportBundler();
};

// replies to /bench/reset once stats have been cleared
struct BenchResetAckBundler : Bundler {
  BenchResetAckBundler(uint64_t generation);
};

// replies to a /bench toggle route (e.g. /bench/overlay_cache) on
// <address>/ack with the resulting state
struct BenchToggleAckBundler : Bundler {
  BenchToggleAckBundler(const std::string& address, bool enabled);
};

#endif
