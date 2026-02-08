#include "rack.hpp"

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

// Network adapter information for IPv4 interfaces
struct AdapterInfo {
    std::string name;           // Adapter identifier (e.g., "eth0", GUID on Windows)
    std::string description;    // Human-readable adapter name
    std::string ip_address;     // IPv4 address in dotted-decimal notation
    std::string netmask;        // IPv4 netmask in dotted-decimal notation
    bool is_connected;          // Whether the adapter is operational and has an IP
    bool is_wifi;               // True if wireless adapter
    bool is_ethernet;           // True if wired Ethernet adapter
    bool is_loopback;           // True if loopback interface
};

// ============================================================================
// Internal Helper Functions
// ============================================================================

namespace detail {

// Convert IP address string to 32-bit integer (host byte order)
inline bool ip_string_to_uint32(const std::string& ip_str, uint32_t& result) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str.c_str(), &addr) != 1) {
        return false;
    }
    result = ntohl(addr.s_addr);
    return true;
}

// Convert 32-bit integer to IP address string (host byte order to dotted-decimal)
inline bool uint32_to_ip_string(uint32_t ip_int, std::string& result) {
    in_addr addr;
    addr.s_addr = htonl(ip_int);
    char buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &addr, buffer, INET_ADDRSTRLEN) == nullptr) {
        return false;
    }
    result = buffer;
    return true;
}

// Convert sockaddr to IP address string
inline bool sockaddr_to_ip_string(const struct sockaddr* sa, std::string& result) {
    if (sa == nullptr || sa->sa_family != AF_INET) {
        return false;
    }
    const struct sockaddr_in* sa_in = reinterpret_cast<const struct sockaddr_in*>(sa);
    char buffer[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &(sa_in->sin_addr), buffer, INET_ADDRSTRLEN) == nullptr) {
        return false;
    }
    result = buffer;
    return true;
}

#ifdef ARCH_WIN
// Convert prefix length (CIDR) to netmask string for Windows
inline bool prefix_length_to_netmask(uint8_t prefix_len, std::string& netmask) {
    uint32_t mask = 0;
    for (int i = 0; i < prefix_len; i++) {
        mask |= (1 << (31 - i));
    }
    return uint32_to_ip_string(mask, netmask);
}

// Check if Windows adapter description indicates a virtual adapter
inline bool should_skip_virtual_adapter(const std::string& description) {
    return description.find("Virtual") != std::string::npos;
}

// Check if Windows adapter is usable (proper type and operational)
inline bool is_adapter_usable(const IP_ADAPTER_ADDRESSES* adapter) {
    if (adapter == nullptr) return false;
    if (adapter->OperStatus != IfOperStatusUp) return false;
    if (adapter->IfType != IF_TYPE_ETHERNET_CSMACD &&
        adapter->IfType != IF_TYPE_IEEE80211) return false;
    return true;
}

// Convert Windows wide string description to UTF-8
inline bool convert_adapter_description(const WCHAR* wide_desc, std::string& result) {
    if (wide_desc == nullptr) return false;
    int len = WideCharToMultiByte(CP_UTF8, 0, wide_desc, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return false;
    std::vector<char> buffer(len);
    WideCharToMultiByte(CP_UTF8, 0, wide_desc, -1, buffer.data(), len, NULL, NULL);
    result = buffer.data();
    return true;
}

// Enumerate network adapters on Windows
inline std::vector<AdapterInfo> get_adapters_windows() {
    std::vector<AdapterInfo> adapters;
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
    ULONG outBufLen = 0;

    // Get required buffer size
    GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen);

    // Allocate and retrieve adapter information
    pAddresses = static_cast<PIP_ADAPTER_ADDRESSES>(malloc(outBufLen));
    if (pAddresses == nullptr) return adapters;

    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr,
                             pAddresses, &outBufLen) != NO_ERROR) {
        free(pAddresses);
        return adapters;
    }

    // Process each adapter
    for (PIP_ADAPTER_ADDRESSES pAdapter = pAddresses; pAdapter != nullptr;
         pAdapter = pAdapter->Next) {

        if (!is_adapter_usable(pAdapter)) continue;

        AdapterInfo adapter;
        adapter.name = pAdapter->AdapterName;

        if (!convert_adapter_description(pAdapter->Description, adapter.description)) continue;
        if (should_skip_virtual_adapter(adapter.description)) continue;

        adapter.is_wifi = (pAdapter->IfType == IF_TYPE_IEEE80211);
        adapter.is_ethernet = (pAdapter->IfType == IF_TYPE_ETHERNET_CSMACD);
        adapter.is_connected = (pAdapter->FirstUnicastAddress != nullptr);
        adapter.is_loopback = false;

        if (!adapter.is_connected) continue;

        // Extract IPv4 address and netmask from unicast addresses
        for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAdapter->FirstUnicastAddress;
             pUnicast != nullptr; pUnicast = pUnicast->Next) {

            if (sockaddr_to_ip_string(pUnicast->Address.lpSockaddr, adapter.ip_address)) {
                prefix_length_to_netmask(pUnicast->OnLinkPrefixLength, adapter.netmask);
                break;
            }
        }

        if (!adapter.ip_address.empty()) {
            adapters.push_back(adapter);
        }
    }

    free(pAddresses);
    return adapters;
}

#else

// Determine if Unix interface name indicates WiFi adapter
inline bool is_wifi_interface(const std::string& name) {
    return name.find("wlan") != std::string::npos ||
           name.find("wifi") != std::string::npos ||
           name.find("wlp") != std::string::npos ||
           name == "en0" || name == "en1";  // macOS WiFi
}

// Determine if Unix interface name indicates Ethernet adapter
inline bool is_ethernet_interface(const std::string& name) {
    return name.find("eth") != std::string::npos ||
           name.find("enp") != std::string::npos ||
           name == "en1" || name == "en2";  // macOS Ethernet
}

// Enumerate network adapters on Unix/Linux/macOS
inline std::vector<AdapterInfo> get_adapters_unix() {
    std::vector<AdapterInfo> adapters;
    struct ifaddrs* ifap = nullptr;

    if (getifaddrs(&ifap) == -1) return adapters;

    // Process each interface
    for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        AdapterInfo adapter;
        adapter.name = ifa->ifa_name;
        adapter.description = "";
        adapter.is_connected = (ifa->ifa_flags & IFF_UP) != 0;
        adapter.is_loopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;
        adapter.is_wifi = is_wifi_interface(adapter.name);
        adapter.is_ethernet = is_ethernet_interface(adapter.name);

        // Extract IP address and netmask
        if (sockaddr_to_ip_string(ifa->ifa_addr, adapter.ip_address) &&
            sockaddr_to_ip_string(ifa->ifa_netmask, adapter.netmask)) {
            adapters.push_back(adapter);
        }
    }

    freeifaddrs(ifap);
    return adapters;
}

#endif

} // namespace detail

// ============================================================================
// Public API
// ============================================================================

// Get all network adapters with IPv4 addresses
inline std::vector<AdapterInfo> get_all_adapters() {
#ifdef ARCH_WIN
    return detail::get_adapters_windows();
#else
    return detail::get_adapters_unix();
#endif
}

// Get IP address and netmask for the preferred network adapter
// Preference order: WiFi > Ethernet > any connected adapter
inline bool get_network_info(std::string& ip_address, std::string& netmask) {
    std::vector<AdapterInfo> adapters = get_all_adapters();
    if (adapters.empty()) return false;

    // Prefer WiFi adapters
    for (const auto& adapter : adapters) {
        if (adapter.is_wifi && adapter.is_connected) {
            ip_address = adapter.ip_address;
            netmask = adapter.netmask;
            return true;
        }
    }

    // Fall back to Ethernet adapters
    for (const auto& adapter : adapters) {
        if (adapter.is_ethernet && adapter.is_connected) {
            ip_address = adapter.ip_address;
            netmask = adapter.netmask;
            return true;
        }
    }

    // Use any connected adapter with an IP address
    for (const auto& adapter : adapters) {
        if (adapter.is_connected && !adapter.ip_address.empty()) {
            ip_address = adapter.ip_address;
            netmask = adapter.netmask;
            return true;
        }
    }

    return false;
}

// Calculate broadcast address for the preferred network adapter
// Formula: broadcast = (ip & netmask) | ~netmask
inline bool calculate_broadcast_address(std::string& broadcast_address) {
    std::string ip_address, netmask;
    if (!get_network_info(ip_address, netmask)) {
        return false;
    }

    uint32_t ip_int, mask_int;
    if (!detail::ip_string_to_uint32(ip_address, ip_int)) return false;
    if (!detail::ip_string_to_uint32(netmask, mask_int)) return false;

    // Calculate: network_address = ip & mask, broadcast = network | ~mask
    uint32_t network_int = ip_int & mask_int;
    uint32_t broadcast_int = network_int | (~mask_int);

    return detail::uint32_to_ip_string(broadcast_int, broadcast_address);
}

} // namespace Network
