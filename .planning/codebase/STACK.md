# Technology Stack

**Analysis Date:** 2025-01-15

## Languages

**Primary:**
- C++ C++20 - Core plugin implementation, OSC protocol handling, module logic
- Shell (Bash) - Build orchestration scripts

**Build Support:**
- Makefile - Build system configuration

## Runtime

**Environment:**
- VCV Rack 2.x - Plugin framework and runtime host

**Compilation Targets:**
- Windows (MinGW via MSYS2)
- Linux (WSL2 and native)
- macOS

## Frameworks

**Core:**
- VCV Rack API 2.x - Audio plugin framework providing module system, UI widgets, audio processing

**Networking:**
- oscpack - OSC (Open Sound Control) packet marshaling and UDP networking library

**Build/Dev:**
- GNU Make - Build orchestration (via `$(RACK_DIR)/plugin.mk`)

## Key Dependencies

**Critical:**
- `oscpack` - Open Sound Control packet library with UDP networking support. Provides:
  - `osc::OutboundPacketStream` for message packing
  - `osc::OscPacketListener` for receiving OSC messages
  - Platform-specific UDP socket implementations (Win32 and POSIX)
  - UDP broadcasting and direct IP communication

**Infrastructure:**
- `qoi` (Quite OK Image) - Header-only image format for efficient texture encoding/decoding used in `src/texture/`
- `rapidhash` - Fast non-cryptographic hash function used in `src/texture/Catalog.cpp` for texture content hashing
- `stb_image_write.h` - Single-header image encoding library (included but not actively used, commented out)

**Platform Libraries:**
- Windows: `ws2_32` (Windows Sockets 2), `winmm` (Windows Multimedia), `iphlpapi` (IP Helper API for network adapter enumeration)
- macOS: `CoreFoundation`, `SystemConfiguration` frameworks
- POSIX: Standard network sockets (`sys/socket.h`), interface address functions (`ifaddrs.h`)

## Configuration

**Environment:**
- `RACK_DIR` - Root directory of VCV Rack installation (default: `../../` from plugin root)
  - Used to include plugin build system (`$(RACK_DIR)/arch.mk`, `$(RACK_DIR)/plugin.mk`)
  - Links against Rack libraries at `$(RACK_DIR)/dep/lib`

**Compiler Flags:**
- `-std=c++20` - C++20 standard (enforced, removes any `-std=c++11` from inherited flags)
- `-I./dependencies` - Include path for local dependencies

**Linker Flags (Platform-specific):**

**Windows (MinGW):**
```
-lws2_32        # Windows Sockets API
-lwinmm         # Windows Multimedia Services  
-liphlpapi      # IP Helper API
-lopengl32      # OpenGL
-L$(RACK_DIR)/dep/lib
```

**Linux/POSIX:**
```
-lstdc++        # C++ Standard Library
```

**macOS:**
```
-framework CoreFoundation
-framework SystemConfiguration
```

## Build System Details

**Source Organization:**
- `src/*.cpp` - Main plugin implementation
- `src/texture/*.cpp` - Image rendering and texture management
- `src/util/*.cpp` - Utility functions including network enumeration
- `src/osc/*.cpp` - OSC protocol implementation
- `src/osc/Bundler/*.cpp` - OSC message bundling strategies
- `src/osc/MessagePacker/*.cpp` - OSC message packing
- `src/osc/ChunkedSend/*.cpp` - Large message fragmentation handling
- `dependencies/oscpack/ip/*.cpp` + platform-specific (`win32/*.cpp` or `posix/*.cpp`)
- `dependencies/oscpack/osc/*.cpp` - OSC packet marshaling

**Build Artifacts:**
- Windows: `plugin.dll`
- Linux/macOS: `plugin.so`

**Distribution Package:**
- Created via `make dist`
- Includes: compiled plugin, `plugin.json`, `res/` directory, `LICENSE*`, dependency licenses, presets

## Platform Detection

Build system uses compiler's `dumpmachine` output to auto-detect MinGW/MSYS2:
- If MinGW detected → Use Windows networking (`win32/*.cpp` sources)
- Otherwise → Use POSIX networking (`posix/*.cpp` sources)

**Multi-Build Isolation:**
- `build-wsl.sh` → `build-wsl/` cache (WSL2/Linux)
- `build-msys.sh` → `build-msys/` cache (Windows MSYS2)
- Prevents build artifact conflicts when same repository used in multiple environments

---

*Stack analysis: 2025-01-15*
