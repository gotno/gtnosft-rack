#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Presets.hpp"

// Binary audio packet. See API.md "Audio packet format".
namespace bcast {

constexpr uint8_t MAGIC[2] = {'R', 'A'};
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t HEADER_SIZE = 20;
constexpr size_t MAX_DATAGRAM = 1232;
constexpr int DEFAULT_PORT = 7747;

struct PacketHeader {
  uint32_t streamId;
  uint16_t epoch;
  Codec codec;
  uint16_t frameSamples;
  uint8_t presetId;
  uint32_t seq; // newest frame
};

using EncodedFrame = std::vector<uint8_t>;

// largest encoded frame that still lets frameCount of them fit one datagram
constexpr size_t maxFrameBytes(int frameCount) {
  return (MAX_DATAGRAM - HEADER_SIZE) / frameCount - 2;
}

// frames newest first. returns the packet size, or 0 if they don't fit
size_t writePacket(
  uint8_t* out,
  size_t cap,
  const PacketHeader& header,
  const std::vector<const EncodedFrame*>& frames
);

}
