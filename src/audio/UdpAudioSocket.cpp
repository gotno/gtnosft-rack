#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <mmsystem.h>
  #ifndef SIO_UDP_CONNRESET
    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
  #endif
#else
  #include <cerrno>
  #include <cstring>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/ip.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include "UdpAudioSocket.hpp"

#ifdef _WIN32
// The qWAVE subset we use, declared here because the toolchain's mingw-w64
// qos2.h is broken (uses QOS_FLOWID before its typedef, lacks
// QOS_NON_ADAPTIVE_FLOW). Layouts match the Windows SDK; links to -lqwave.
extern "C" {
struct QOS_VERSION {
  USHORT MajorVersion;
  USHORT MinorVersion;
};
enum QOS_TRAFFIC_TYPE {
  QOSTrafficTypeBestEffort,
  QOSTrafficTypeBackground,
  QOSTrafficTypeExcellentEffort,
  QOSTrafficTypeAudioVideo,
  QOSTrafficTypeVoice,
  QOSTrafficTypeControl
};
typedef UINT32 QOS_FLOWID;
BOOL WINAPI QOSCreateHandle(QOS_VERSION* Version, HANDLE* QOSHandle);
BOOL WINAPI QOSCloseHandle(HANDLE QOSHandle);
BOOL WINAPI QOSAddSocketToFlow(HANDLE QOSHandle, SOCKET Socket, PSOCKADDR DestAddr,
                               QOS_TRAFFIC_TYPE TrafficType, DWORD Flags, QOS_FLOWID* FlowId);
BOOL WINAPI QOSRemoveSocketFromFlow(HANDLE QOSHandle, SOCKET Socket, QOS_FLOWID FlowId, DWORD Flags);
}
constexpr DWORD QOS_NON_ADAPTIVE_FLOW = 0x00000002;
#endif

namespace bcast {

namespace {

#ifdef _WIN32
using Native = SOCKET;
const Native NATIVE_INVALID = INVALID_SOCKET;

void ensureNetworking() {
  struct Init {
    Init() { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
    ~Init() { WSACleanup(); }
  };
  static Init init;
}

void closeNative(Native s) { closesocket(s); }
std::string lastSocketError() { return "error " + std::to_string(WSAGetLastError()); }
#else
using Native = int;
const Native NATIVE_INVALID = -1;

void ensureNetworking() {}
void closeNative(Native s) { ::close(s); }
std::string lastSocketError() { return std::strerror(errno); }
#endif

Native native(intptr_t s) { return (Native)s; }

}

bool UdpAudioSocket::open(const std::string& host, int port) {
  close();
  ensureNetworking();
  error.clear();

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  addrinfo* found = nullptr;
  std::string service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || !found) {
    error = "can't resolve " + host;
    return false;
  }

  Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == NATIVE_INVALID) {
    error = "socket: " + lastSocketError();
    freeaddrinfo(found);
    return false;
  }
  if (::connect(s, found->ai_addr, (int)found->ai_addrlen) != 0) {
    error = "connect: " + lastSocketError();
    closeNative(s);
    freeaddrinfo(found);
    return false;
  }
  freeaddrinfo(found);

  sock = (intptr_t)s;
  markPriority();
  return true;
}

void UdpAudioSocket::markPriority() {
  Native s = native(sock);
#ifdef _WIN32
  // otherwise an ICMP port-unreachable makes later sends fail
  BOOL report = FALSE;
  DWORD bytes = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &bytes, nullptr, nullptr);

  // Windows ignores IP_TOS; qWAVE marks voice traffic (DSCP 46, WMM AC_VO)
  QOS_VERSION version{1, 0};
  HANDLE handle = nullptr;
  if (QOSCreateHandle(&version, &handle)) {
    QOS_FLOWID flow = 0;
    if (QOSAddSocketToFlow(handle, s, nullptr, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &flow)) {
      qosHandle = handle;
      qosFlow = flow;
      marked = true;
    } else {
      QOSCloseHandle(handle);
    }
  }
#else
  int tos = 0xB8;  // DSCP EF (46)
  marked = setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == 0;
  #ifdef __linux__
  int priority = 6;  // the highest without CAP_NET_ADMIN; maps to WMM voice
  setsockopt(s, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
  #endif
#endif
}

void UdpAudioSocket::close() {
  if (sock == INVALID) return;
#ifdef _WIN32
  if (qosHandle) {
    QOSRemoveSocketFromFlow((HANDLE)qosHandle, native(sock), qosFlow, 0);
    QOSCloseHandle((HANDLE)qosHandle);
    qosHandle = nullptr;
    qosFlow = 0;
  }
#endif
  closeNative(native(sock));
  sock = INVALID;
  marked = false;
}

bool UdpAudioSocket::send(const uint8_t* data, size_t size) {
  if (sock == INVALID) return false;
#ifdef _WIN32
  int sent = ::send(native(sock), (const char*)data, (int)size, 0);
  if (sent == SOCKET_ERROR) {
    int e = WSAGetLastError();
    return e == WSAECONNRESET || e == WSAECONNREFUSED;
  }
#else
  ssize_t sent = ::send(native(sock), data, size, 0);
  if (sent < 0) return errno == ECONNREFUSED || errno == EINTR;
#endif
  return true;
}

void setHighResolutionTimer(bool on) {
#ifdef _WIN32
  if (on) timeBeginPeriod(1);
  else timeEndPeriod(1);
#else
  (void)on;
#endif
}

}
