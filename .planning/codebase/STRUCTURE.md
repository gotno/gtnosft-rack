# Codebase Structure

**Analysis Date:** 2025-01-06

## Directory Layout

```
gtnosft-rack/
├── src/                          # All C++ source code
│   ├── plugin.cpp                # Plugin initialization (registers OSCctrl model)
│   ├── plugin.hpp                # Plugin export declarations
│   ├── OSCctrl.hpp               # Main widget and module definitions
│   ├── OSCctrl.cpp               # Widget lifecycle, action queue, module stub
│   ├── osc/                      # OSC protocol implementation
│   │   ├── OscSender.hpp         # Outbound message queue and transmission
│   │   ├── OscSender.cpp         # Queue worker thread, UDP socket management
│   │   ├── OscReceiver.hpp       # Inbound message listener and router
│   │   ├── OscReceiver.cpp       # Listener thread, heartbeat, route handlers
│   │   ├── OscConstants.hpp      # Buffer sizes, ports, timing constants
│   │   ├── ChunkedManager.hpp    # Chunked transfer lifecycle management
│   │   ├── ChunkedManager.cpp    # Add/ack/process for large payloads
│   │   ├── SubscriptionManager.hpp  # Polling state change dispatcher
│   │   ├── SubscriptionManager.cpp  # Subscription ticking, module light polling
│   │   ├── Bundler/              # Message serialization strategies (abstract + concrete)
│   │   │   ├── Bundler.hpp       # Abstract base class for all bundlers
│   │   │   ├── BroadcastHeartbeatBundler.hpp/cpp   # Broadcast mode keepalive
│   │   │   ├── DirectHeartbeatBundler.hpp/cpp       # Direct unicast keepalive
│   │   │   ├── PatchInfoBundler.hpp/cpp             # Global patch metadata
│   │   │   ├── ModuleStubsBundler.hpp/cpp           # Basic module list
│   │   │   ├── ModuleStructureBundler.hpp/cpp       # Module parameters structure
│   │   │   ├── ModuleStateBundler.hpp/cpp           # Module position and texture
│   │   │   ├── ModuleParamsBundler.hpp/cpp          # Parameter values with caching
│   │   │   ├── ModuleLightsBundler.hpp/cpp          # Light colors with caching
│   │   │   ├── CablesBundler.hpp/cpp                # Cable connections
│   │   │   ├── ChunkedImageBundler.hpp/cpp          # Image chunk transmission
│   │   │   └── ChunkedSendBundler.hpp/cpp           # Abstract base for chunked transfers
│   │   └── ChunkedSend/          # Large payload handling
│   │       ├── ChunkedSend.hpp    # Abstract chunk lifecycle and retry logic
│   │       ├── ChunkedSend.cpp    # ACK tracking, failure detection
│   │       ├── ChunkedImage.hpp   # Compressed image payload
│   │       └── ChunkedImage.cpp   # QOI compression, bundler creation
│   ├── texture/                  # Rendering and caching
│   │   ├── Renderer.hpp          # Pixel rendering from VCV Rack widgets
│   │   ├── Renderer.cpp          # Framebuffer capture, scaling, layering
│   │   ├── Catalog.hpp           # Rendered texture caching and deduplication
│   │   └── Catalog.cpp           # Overlay ID management
│   └── util/                     # Utility helpers
│       ├── Timer.hpp             # Promise-based recurring intervals
│       ├── Network.hpp           # Broadcast address discovery
│       ├── Util.hpp              # Math utilities (vec2cm)
│       └── Util.cpp              # Conversion implementations
├── res/                          # Resource assets
│   └── OSCctrl.svg               # Module panel SVG
├── plugin.json                   # Plugin metadata (name, version, slug)
├── Makefile                      # Build configuration
├── build-*.sh                    # Build scripts for Windows/Linux/macOS
├── dep/                          # External dependencies (git submodules)
│   ├── oscpack/                  # OSC library (UDP sockets, parsing)
│   ├── qoi/                      # QOI image codec (compression)
│   └── rack/                     # VCV Rack SDK
└── dependencies/                 # System package manager dependencies
```

## Directory Purposes

**src/**
- Purpose: All plugin source code
- Contains: C++ headers and implementations, organized by subsystem
- Key files: Entry points are `plugin.cpp` and `OSCctrl.cpp`

**src/osc/**
- Purpose: OSC protocol abstraction and message handling
- Contains: Sender (outbound queue), Receiver (inbound router), routing infrastructure
- Key files: `OscSender.cpp` (worker thread), `OscReceiver.cpp` (listener thread), `OscConstants.hpp` (shared config)

**src/osc/Bundler/**
- Purpose: Pluggable message serialization strategies
- Contains: Abstract `Bundler` base class and 10+ concrete implementations
- Key files: `Bundler.hpp` (interface), implementations organized by data domain (patch, module, cable, image, heartbeat)
- Pattern: Each subclass populates `messages` vector with (OSC path, serializer lambda) pairs

**src/osc/ChunkedSend/**
- Purpose: Large payload fragmentation and retransmission
- Contains: Abstract lifecycle, concrete image implementation, QOI codec integration
- Key files: `ChunkedSend.hpp` (ACK tracking), `ChunkedImage.cpp` (compression)

**src/texture/**
- Purpose: Rendering VCV Rack widgets to pixel buffers
- Contains: Framebuffer management, scaling logic, texture caching
- Key files: `Renderer.hpp` (static methods for type-specific renders), `Catalog.hpp` (deduplication)

**src/util/**
- Purpose: Low-level helpers
- Contains: Timer abstraction, network utilities, math conversions
- Key files: `Timer.hpp` (promise-based intervals), `Network.hpp` (broadcast discovery)

## Key File Locations

**Entry Points:**
- `src/plugin.cpp`: Called by VCV Rack at plugin load; registers `modelOSCctrl`
- `src/OSCctrl.cpp`: OSCctrlWidget constructor; initializes all subsystems
- `src/osc/OscSender.cpp`: Queue worker thread; processes bundler messages
- `src/osc/OscReceiver.cpp`: Listener thread; routes incoming OSC messages

**Configuration:**
- `plugin.json`: Plugin metadata, version, brand
- `src/osc/OscConstants.hpp`: MSG_BUFFER_SIZE (1452 bytes), RX_PORT (7225), TX_PORT (7746), heartbeat/subscription timings
- `Makefile`: Build rules, compiler flags, dependency linking

**Core Logic:**
- `src/OSCctrl.hpp`: OSCctrlWidget and ModuleWidget coordination
- `src/osc/OscSender.hpp`: Message queue interface and endpoint management
- `src/osc/OscReceiver.hpp`: Listener socket and route map
- `src/osc/ChunkedManager.hpp`: Chunked transfer state machine
- `src/osc/Bundler/Bundler.hpp`: Abstract message builder interface

**Testing/Development:**
- `res/OSCctrl.svg`: Panel layout for visual inspection in Rack
- Build outputs: `build/` (intermediates), `dist/` (packaged plugin), `plugin.so`/`plugin.dll` (compiled)

## Naming Conventions

**Files:**
- Headers: `.hpp` (C++, VCV Rack convention)
- Sources: `.cpp` (C++, VCV Rack convention)
- SVG assets: PascalCase (e.g., `OSCctrl.svg`)
- Config: lowercase with dots (e.g., `plugin.json`, `compile_commands.json`)

**Directories:**
- subsystems: lowercase (osc, texture, util)
- Concepts: PascalCase (Bundler, ChunkedSend)

**Classes:**
- Structs (VCV convention): PascalCase (OSCctrl, OSCctrlWidget, OscSender, Bundler)
- With verbs: camelCase action names (sendHeartbeat, processQueue, enqueueAction)
- Static data members: camelCase (idCounter, lights, params)

**Functions:**
- Methods: camelCase (enqueueBundler, processActionQueue, subscribeModuleLights)
- Lifecycle: {verb}{Noun} (startListener, endListener, startQueueWorker)
- Predicates: is/has prefix (isBroadcasting, hasRemainingMessages, isProcessing)

**Bundler Naming:**
- `{Domain}Bundler`: e.g., `ModuleStateBundler`, `CablesBundler`, `ChunkedImageBundler`
- Heartbeat types: `{SendMode}HeartbeatBundler`: e.g., `BroadcastHeartbeatBundler`

**Constants:**
- OSC paths: lowercase with slashes (e.g., `/set/s/m`, `/subscribe/lights`)
- Config: SCREAMING_SNAKE_CASE (MSG_BUFFER_SIZE, RX_PORT, MAX_MISSED_HEARTBEATS)

**Variables:**
- Modern: camelCase (moduleWidget, textureId, unackedChunkNums)
- Private member pointers: camelCase with trailing zero (osctx, chunkman, subman, ctrl)

## Where to Add New Code

**New Feature (e.g., temperature monitoring):**
- Primary state bundler: `src/osc/Bundler/ModuleTemperatureBundler.hpp/cpp`
- Routing handler: Add case in `OscReceiver::generateRoutes()` → `src/osc/OscReceiver.cpp`
- Subscription support (optional): Add SubscriptionType enum value, `SubscriptionManager::subscribeModuleTemperature()`, create `ModuleTemperatureBundler` variant with callback
- Tests: Create `test_ModuleTemperatureBundler.cpp` alongside implementation

**New Chunked Transfer Type (e.g., video stream):**
- Abstract subclass: `src/osc/ChunkedSend/ChunkedVideo.hpp/cpp` (inherit from ChunkedSend)
- Bundler: `src/osc/Bundler/ChunkedVideoBundler.hpp/cpp` (inherit from ChunkedSendBundler)
- Codec integration: Add library to `dep/`, include in `Makefile`
- Manager integration: ChunkedManager.add() already generic; just instantiate ChunkedVideo subclass

**New Rendering Type (e.g., custom widget):**
- Renderer method: `static RenderResult renderCustomWidget(widget, recipe)` in `src/texture/Renderer.hpp`
- Implementation: Add to `src/texture/Renderer.cpp` following pattern of `renderPanel()`, `renderKnob()`, etc.
- Catalog entry: Optional; add TextureType enum variant if caching needed

**New Module Feature (if plugin adds module beyond OSCctrl):**
- Module struct: `src/MyModule.hpp` (Module subclass)
- Widget struct: `src/MyModule.cpp` (ModuleWidget subclass, includes constructor/step/menu)
- Registration: Add to `src/plugin.cpp` init() via `p->addModel(modelMyModule)`

**Utility Helper:**
- Location: `src/util/MyHelper.hpp` (header-only) or `src/util/MyHelper.hpp/cpp`
- Naming: Wrap in namespace if general-purpose (e.g., `gtnosft::util::`); module-local if specific
- Example: `Util.hpp` contains `vec2cm()` for coordinate conversion

## Special Directories

**dep/ (Dependencies):**
- Purpose: Git submodules for external libraries
- Contents: oscpack (OSC), qoi (image codec), rack (VCV Rack SDK)
- Generated: No
- Committed: Yes (submodule references, not full code)
- Management: `git submodule update --init`

**build/ (Build Intermediates):**
- Purpose: Compiled objects, CMake cache, linking artifacts
- Generated: Yes (by make)
- Committed: No (in .gitignore)
- Cleanup: `make clean` or `rm -rf build/`

**dist/ (Distribution Package):**
- Purpose: Packaged plugin archive for distribution
- Generated: Yes (by `make dist`)
- Committed: No (in .gitignore)
- Contents: Compiled plugin binary (plugin.so/.dll), panel SVG, metadata

**res/ (Resources):**
- Purpose: Static assets (SVG panels, textures, icons)
- Contents: OSCctrl.svg (module panel)
- Generated: No
- Committed: Yes
- Usage: Loaded via `asset::plugin(pluginInstance, "res/OSCctrl.svg")`

---

*Structure analysis: 2025-01-06*
