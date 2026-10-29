#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace bcast {

// connected UDP socket for audio, marked for voice priority where the OS allows
class UdpAudioSocket {
public:
  UdpAudioSocket() = default;
  ~UdpAudioSocket() { close(); }
  UdpAudioSocket(const UdpAudioSocket&) = delete;
  UdpAudioSocket& operator=(const UdpAudioSocket&) = delete;

  bool open(const std::string& host, int port);
  void close();
  // false on a real failure. ICMP port-unreachable bounces count as success
  bool send(const uint8_t* data, size_t size);
  bool isOpen() const { return sock != INVALID; }
  bool priorityMarked() const { return marked; }
  const std::string& lastError() const { return error; }

private:
  static constexpr intptr_t INVALID = -1;
  intptr_t sock = INVALID;
  bool marked = false;
  void* qosHandle = nullptr;
  uint32_t qosFlow = 0;
  std::string error;

  void markPriority();
};

// 1 ms scheduler resolution on Windows; a no-op elsewhere. Calls must pair
void setHighResolutionTimer(bool on);

}
