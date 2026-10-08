#include "BenchReportBundler.hpp"

#ifdef GTNOSFT_BENCH

BenchReportBundler::BenchReportBundler(): Bundler("BenchReportBundler") {
  bench::Report report = bench::report();

  messages.emplace_back(
    "/bench/report/begin",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << (osc::int64)report.generation << report.windowSec;
    }
  );

  for (const bench::Summary& stat : report.stats) {
    messages.emplace_back(
      "/bench/stat",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << stat.name.c_str()
          << stat.count
          << stat.mean
          << stat.min
          << stat.p50
          << stat.p95
          << stat.p99
          << stat.max
          ;
      }
    );
  }

  for (const auto& [name, value] : report.counters) {
    messages.emplace_back(
      "/bench/counter",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << name.c_str() << (osc::int64)value;
      }
    );
  }

  for (const bench::TextureRate& texture : report.textures) {
    messages.emplace_back(
      "/bench/texture",
      [=](osc::OutboundPacketStream& pstream) {
        pstream << (osc::int64)texture.textureId
          << texture.kind.c_str()
          << texture.completed
          << texture.failed
          << texture.fps
          ;
      }
    );
  }

  // lets the client detect dropped report packets
  int32_t numMessages = (int32_t)messages.size() + 1;
  messages.emplace_back(
    "/bench/report/end",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << numMessages;
    }
  );
}

BenchResetAckBundler::BenchResetAckBundler(
  uint64_t generation
): Bundler("BenchResetAckBundler") {
  messages.emplace_back(
    "/bench/reset/ack",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << (osc::int64)generation;
    }
  );
}

#endif
