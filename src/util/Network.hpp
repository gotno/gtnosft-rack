#ifdef ARCH_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <iostream>
#include <stdexcept>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <cstring>
#include <unistd.h>
#endif

#include <vector>
#include <string>

namespace Network {

struct AdapterInfo {
    std::string name;
    std::string description;
    std::string ip_address;
    std::string netmask;
    bool is_connected;
    bool is_wifi;
    bool is_ethernet;
    bool is_loopback;
};

std::vector<AdapterInfo> get_all_adapters() {
  std::vector<AdapterInfo> adapters;
  
#ifdef ARCH_WIN
  // initialize variables
  PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
  ULONG outBufLen = 0;
  
  // first call to get the required buffer size
  GetAdaptersAddresses(
    AF_INET,
    GAA_FLAG_INCLUDE_PREFIX,
    nullptr,
    pAddresses,
    &outBufLen
  );
  
  // allocate memory
  pAddresses = (PIP_ADAPTER_ADDRESSES)malloc(outBufLen);
  if (pAddresses == nullptr) {
    return adapters;
  }
  
  // second call to get actual data
  if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen) != NO_ERROR) {
    free(pAddresses);
    return adapters;
  }
  
  // iterate through all adapters
  for (PIP_ADAPTER_ADDRESSES pAdapter = pAddresses; pAdapter != nullptr; pAdapter = pAdapter->Next) {
    // skip disabled adapters
    if (pAdapter->OperStatus != IfOperStatusUp) continue;
      
    AdapterInfo adapter;
      
    // convert wide string to narrow string for adapter name
    adapter.name = std::string(pAdapter->AdapterName);
      
    // convert wide string to narrow string for description
    int len = WideCharToMultiByte(CP_UTF8, 0, pAdapter->Description, -1, NULL, 0, NULL, NULL);
    if (len > 0) {
      std::vector<char> buffer(len);
      WideCharToMultiByte(CP_UTF8, 0, pAdapter->Description, -1, buffer.data(), len, NULL, NULL);
      adapter.description = std::string(buffer.data());
    }
      
    // determine if it's wifi or ethernet
    adapter.is_wifi =
      adapter.description.find("Wi-Fi") != std::string::npos || 
      adapter.description.find("Wireless") != std::string::npos;
    adapter.is_ethernet =
      adapter.description.find("Ethernet") != std::string::npos || 
      pAdapter->IfType == IF_TYPE_ETHERNET_CSMACD;
      
    // check if it's connected (has an IP address)
    adapter.is_connected = pAdapter->OperStatus == IfOperStatusUp && pAdapter->FirstUnicastAddress != nullptr;
      
    // look at all unicast addresses
    for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAdapter->FirstUnicastAddress; 
      pUnicast != nullptr; 
      pUnicast = pUnicast->Next) {
          
      if (pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
        // get IP address
        SOCKADDR_IN* sockaddr_ipv4 = reinterpret_cast<SOCKADDR_IN*>(pUnicast->Address.lpSockaddr);
        char ip_buffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ip_buffer, INET_ADDRSTRLEN);
        adapter.ip_address = ip_buffer;
              
        // convert prefix length to netmask
        ULONG mask = 0;
        for (int i = 0; i < pUnicast->OnLinkPrefixLength; i++) {
          mask |= (1 << (31 - i));
        }
        in_addr netmask_addr;
        netmask_addr.s_addr = htonl(mask);
        char netmask_buffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &netmask_addr, netmask_buffer, INET_ADDRSTRLEN);
        adapter.netmask = netmask_buffer;
        
        break;
      }
    }
      
    // add to our list if it has an IP address
    if (!adapter.ip_address.empty()) adapters.push_back(adapter);
  }
  
  free(pAddresses);

#else

  struct ifaddrs* ifap = nullptr;
  
  // g list of network interfaces
  if (getifaddrs(&ifap) == -1) return adapters;
  
  // iterate through all interfaces
  for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
    // skip interfaces that don't have addresses
    if (ifa->ifa_addr == nullptr) continue;
      
    // only process IPv4 addresses
    if (ifa->ifa_addr->sa_family == AF_INET) {
      AdapterInfo adapter;
      adapter.name = ifa->ifa_name;
      adapter.is_connected = (ifa->ifa_flags & IFF_UP) != 0;
      adapter.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
      
      // determine interface type based on name
      // common naming conventions:
      // - en0, en1: Ethernet or WiFi on macOS
      // - wlan0: WiFi on Linux
      // - eth0: Ethernet on Linux
      adapter.is_wifi = 
        adapter.name.find("wlan") != std::string::npos ||
        adapter.name.find("wifi") != std::string::npos ||
        adapter.name.find("wlp") != std::string::npos ||
        // on macOS, en0 is usually the WiFi
        (adapter.name == "en0" || adapter.name == "en1");
      
      adapter.is_ethernet = 
        adapter.name.find("eth") != std::string::npos ||
        adapter.name.find("enp") != std::string::npos ||
        // on macOS, en1 or en2 might be Ethernet
        (adapter.name == "en1" || adapter.name == "en2");
      
      // get IP address
      struct sockaddr_in* sa = (struct sockaddr_in*)ifa->ifa_addr;
      char ip_buffer[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(sa->sin_addr), ip_buffer, INET_ADDRSTRLEN);
      adapter.ip_address = ip_buffer;
      
      // get netmask
      sa = (struct sockaddr_in*)ifa->ifa_netmask;
      char netmask_buffer[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(sa->sin_addr), netmask_buffer, INET_ADDRSTRLEN);
      adapter.netmask = netmask_buffer;
      
      adapters.push_back(adapter);
    }
  }
  
  // Free the interface list
  freeifaddrs(ifap);

#endif

  return adapters;
}

bool get_network_info(std::string& ip_address, std::string& netmask) {
  std::vector<AdapterInfo> adapters = get_all_adapters();
  
  if (adapters.empty()) return false;
    
  // prefer wifi
  for (const auto& adapter : adapters) {
    if (adapter.is_wifi && adapter.is_connected) {
      ip_address = adapter.ip_address;
      netmask = adapter.netmask;
      return true;
    }
  }
  
  // if not wifi, maybe ethernet
  for (const auto& adapter : adapters) {
    if (adapter.is_ethernet && adapter.is_connected) {
      ip_address = adapter.ip_address;
      netmask = adapter.netmask;
      return true;
    }
  }
  
  // any connected adapter
  for (const auto& adapter : adapters) {
    if (adapter.is_connected && !adapter.ip_address.empty()) {
      ip_address = adapter.ip_address;
      netmask = adapter.netmask;
      return true;
    }
  }
  
  return false;
}

bool calculate_broadcast_address(std::string& broadcast_address) {
  std::string ip_address, netmask;
  get_network_info(ip_address, netmask);

  // convert ip address to integer
  struct in_addr ip_addr;
  if (inet_pton(AF_INET, ip_address.c_str(), &ip_addr) != 1) {
    return false; // invalid ip address format
  }
  uint32_t ip_int = ntohl(ip_addr.s_addr);
  
  // convert netmask to integer
  struct in_addr mask_addr;
  if (inet_pton(AF_INET, netmask.c_str(), &mask_addr) != 1) {
    return false; // invalid netmask format
  }
  uint32_t mask_int = ntohl(mask_addr.s_addr);

  // calculate the network address (ip & mask)
  uint32_t network_int = ip_int & mask_int;
  
  // calculate the broadcast address (NETWORK | ~MASK)
  uint32_t broadcast_int = network_int | (~mask_int);
  
  // convert broadcast address back to dotted decimal format
  in_addr broadcast_addr;
  broadcast_addr.s_addr = htonl(broadcast_int);
  char broadcast_str[INET_ADDRSTRLEN];
  if (inet_ntop(AF_INET, &broadcast_addr, broadcast_str, INET_ADDRSTRLEN) == nullptr) {
    return false; // conversion failed
  }

  broadcast_address = std::string(broadcast_str);

  return true;
}

} // namespace Network
