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

// replies to /bench/overlay_cache with the resulting cache state
struct BenchOverlayCacheAckBundler : Bundler {
  BenchOverlayCacheAckBundler(bool enabled);
};

#endif
