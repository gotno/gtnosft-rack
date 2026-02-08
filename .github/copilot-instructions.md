```markdown
# Copilot Instructions for gtnosft-rack

## Project Context

**What is OSCctrl?**
OSCctrl is a VCV Rack utility module that provides a network API for remote control and visualization. Unlike typical VCV Rack modules (oscillators, filters, effects), OSCctrl does not process audio. Instead, it exposes VCV Rack's state and control surface over OSC (Open Sound Control) protocol.

**Primary Use Case: VR Backend**
This module serves as the audio engine backend for a VR eurorack modular synthesizer. The VR application:
- Sends OSC commands to control VCV Rack modules (parameter changes, cable connections)
- Receives rendered PNG textures of module panels, knobs, switches, and overlays
- Displays these textures in a 3D virtual eurorack environment
- Subscribes to real-time parameter/light updates for visual feedback

This explains several design decisions:
- **Texture rendering system** - VR needs visual representations of modules
- **Chunked transfer protocol** - Large PNG images require multi-packet delivery with ACK
- **Subscription system** - Real-time streaming of light/parameter states for VR animations
- **Broadcast/direct modes** - Discovery mechanism for VR client to find Rack instance

**Other Potential Use Cases:**
- Headless VCV Rack automation
- DAW integration
- Custom control surfaces
- Remote performance setups
- Automated testing

## Tech Stack & Architecture
- **Language:** C++20
- **Platform:** VCV Rack plugin (modular synth environment)
- **Core Libraries:** [oscpack](https://www.rossbencina.com/code/oscpack) (OSC packet manipulation), qoi (image encoding), stb_image_write
- **Plugin Manifest:** `plugin.json` (metadata, modules, author info)

## Key Directories & Files
- `src/` — Main source code
  - `osc/` — OSC communication logic (Sender, Receiver, Bundler, ChunkedSend)
  - `renderer/` — Rendering logic
  - `util/` — Utility classes
- `dependencies/` — Third-party libraries (oscpack, qoi, stb_image_write)
- `res/` — Resources (SVG graphics)
- `dist/` — Build artifacts (packaged plugins)
- `Makefile` — Main build script
- `.github/workflows/build-plugin.yml` — CI/CD for building and publishing
- `plugin.json` — Plugin manifest

## Build & Test Commands
- **Build (local):**
  - Run `make` (requires VCV Rack repository (or SDK), set `RACK_DIR` if not autodetected)
- **Build (CI):**
  - GitHub Actions workflow: `.github/workflows/build-plugin.yml`
  - Builds for Windows, Linux, and Mac (cross-compilation via containers)
- **Distribution:**
  - Run `make dist` to package plugin for release

## Project Conventions
- All source files are in `src/` and its subdirectories.
- Third-party dependencies are vendored in `dependencies/`.
- Resources (SVG, etc.) are in `res/`.
- Plugin metadata and module definitions are in `plugin.json`.
- License files for dependencies are included in the distributable.

## Notable Modules
- **OSCctrl:** Main module providing an OSC-based API for Rack simulations.

## Licensing
- Plugin: MIT License (see `plugin.json`)
- oscpack: MIT License (`dependencies/oscpack/LICENSE`)
- qoi: MIT License (`dependencies/qoi/LICENSE`)

## Code Conventions
- **2 space tabs**
- **NEVER** vertically align code just for the sake of neatness
- **Naming:**
    - **Classes/Structs:** PascalCase (`OscSender`, `ModuleParamsBundler`, `OSCctrlWidget`)
    - **Methods:** camelCase (`enqueueBundler()`, `setBroadcasting()`, `process()`)
    - **Variables:** camelCase (`osctx`, `chunkman`, `messageWidgets`)
    - **Bundler naming:** `[Data]*Bundler` pattern (e.g., `ModuleStateBundler`, `PatchInfoBundler`)
    - **Widget suffix:** Rack classes ending in `Widget` for UI components

### File Organization
- Header/implementation separation: `.hpp` for declarations, `.cpp` for definitions
- Use forward declarations in headers to minimize includes
- No custom namespaces (uses VCV Rack's `rack::` namespace)

### Design Patterns
- **Bundler pattern:** Abstract base class (`Bundler`) with virtual `isNoop()`, `sent()`, `done()` methods
- **Manager pattern:** Lifecycle management (e.g., `ChunkedManager`, `SubscriptionManager`)
- **Queue-based async:** Action queues with mutex protection for thread safety
- **Widget composition:** `OSCctrlWidget` composes sender/receiver/managers

## Architecture Deep Dive

### Component Overview
```
OSCctrlWidget (main coordinator)
├── OscSender (TX messages via queue worker thread)
├── ChunkedManager (manages large file transfers)
├── SubscriptionManager (real-time streaming)
└── OscReceiver (RX messages via listener thread)
```

**Component initialization order matters:**
```cpp
osctx = new OscSender();                          // 1. Create sender first
chunkman = new ChunkedManager(this, osctx);       // 2. Needs sender
subman = new SubscriptionManager(this, osctx, chunkman); // 3. Needs both
oscrx = new OscReceiver(this, osctx, chunkman, subman);  // 4. Needs all
```

### Thread Model (CRITICAL FOR DEVELOPMENT)

**Three main threads run concurrently:**

1. **VCV Rack main thread** (runs `step()` every frame)
   - ✅ **SAFE:** Create/modify widgets, change parameters, access Rack API
   - Processes `actionQueue` - runs lambdas from other threads

2. **OSC listener thread** (`OscReceiver::listenerThread`)
   - ❌ **UNSAFE:** Cannot touch VCV Rack widgets/modules directly
   - ✅ **SAFE:** Must use `ctrl->enqueueAction(lambda)` to schedule work
   - Receives UDP packets, dispatches to route handlers

3. **OSC sender thread** (`OscSender::queueWorker`)
   - ✅ **SAFE:** Can send messages (doesn't touch widgets)
   - Processes bundler queue, builds/sends OSC messages

4. **Heartbeat thread** (`heartbeatInterval`)
   - Monitors client connection
   - Enqueues heartbeat sends on main thread

**Critical Rule: NEVER access `rack::` objects from OSC threads!**

**Wrong (CRASHES):**
```cpp
routes.emplace("/set/param", [&](auto& args, auto&) {
  int64_t id = (args++)->AsInt64();
  auto mw = APP->scene->rack->getModule(id);  // ❌ CRASH - wrong thread!
  mw->getParam(0)->setValue(1.0f);
});
```

**Correct:**
```cpp
routes.emplace("/set/param", [&](auto& args, auto&) {
  int64_t id = (args++)->AsInt64();
  float value = (args++)->AsFloat();

  ctrl->enqueueAction([id, value]() {  // ✅ Runs on main thread
    auto mw = APP->scene->rack->getModule(id);
    if (!mw) return;
    mw->getParam(0)->setValue(value);
  });
});
```

### Data Flow: OSC Request → Response

**Example: Client requests module structure**

1. Client sends `/get/module_structure <plugin> <module>`
2. `OscReceiver::listenerThread` receives UDP packet
3. Route handler extracts args, enqueues action on main thread
4. Action runs on main thread:
   - Creates `ModuleStructureBundler(pluginSlug, moduleSlug)`
   - Bundler constructor creates temp `ModuleWidget`, extracts UI data
   - Populates `messages` vector with OSC messages
   - Deletes temp widget
   - Calls `osctx->enqueueBundler(bundler)`
5. `OscSender::queueWorker` picks up bundler from queue
6. Calls `bundler->bundle(pstream)` to build OSC bundle
7. Sends bundle(s) via UDP (splits if >1452 bytes)
8. Client receives messages

### Rendering Pipeline

**Purpose:** Convert VCV Rack widgets to PNG images for VR display

**Process:**
1. **Find/create target widget** (e.g., ModuleWidget for a module)
2. **Wrap in FramebufferWidget** - enables offscreen OpenGL rendering
3. **Render to texture:**
   - Call `framebuffer->step()` - updates layout
   - Call `framebuffer->render()` - draws to OpenGL texture
4. **Extract pixels** from OpenGL texture (RGBA array)
5. **Flip vertically** (OpenGL Y-up vs image Y-down)
6. **Compress to PNG** using stb_image_write
7. **Split into chunks** (ChunkedImage) - max 1452 bytes per UDP packet
8. **Send chunks** with ACK protocol - client ACKs each chunk

**Static render methods in `Renderer`:**
- `renderPanel()` - Static panel background (no live data)
- `renderOverlay()` - Live state overlay (param values, lights)
- `renderPort()`, `renderKnob()`, `renderSwitch()`, `renderSlider()` - Individual widgets

### Bundler Pattern Deep Dive

**Base class:** `Bundler` (in `src/osc/Bundler/Bundler.hpp`)

**Key members:**
```cpp
std::vector<std::pair<std::string, messageBuilder>> messages;
size_t messageCursor;  // Current position in messages vector
```

**Lifecycle:**
1. **Construction** - Gather data, populate `messages` vector
2. **isNoop()** check - Skip if no data to send
3. **bundle()** - Iterate messages, call builders, handle buffer overflow
4. **sent()** - Post-send callback
5. **done()** - Always-called cleanup (even if noop)

**Three common patterns:**

1. **Simple bundler** (no widget creation needed):
```cpp
PatchInfoBundler::PatchInfoBundler() {
  std::string filename = rack::system::getFilename(APP->patch->path);
  messages.emplace_back("/set/patch_info", [filename](auto& p) {
    p << ctrlId << filename.c_str();
  });
}
```

2. **Looping bundler** (multiple similar messages):
```cpp
ModuleStubsBundler::ModuleStubsBundler() {
  for (const auto& id : APP->engine->getModuleIds()) {
    auto model = APP->engine->getModule(id)->getModel();
    messages.emplace_back("/set/module_stub", [id, model](auto& p) {
      p << id << model->plugin->slug.c_str() << model->slug.c_str();
    });
  }
}
```

3. **Widget analysis bundler** (creates temp widgets):
```cpp
ModuleStructureBundler::ModuleStructureBundler(slug...) {
  auto mw = gtnosft::util::makeConnectedModuleWidget(model);
  addParamMessages(mw);  // Inspect widget hierarchy
  addPortMessages(mw);
  addLightMessages(mw);
  delete mw;  // Cleanup temp widget
}
```

## Development Workflow

### Quick Reference: Common File Locations

**Adding OSC endpoints (receiving commands):**
- Route registration: `src/osc/OscReceiver.cpp` → `generateRoutes()`
- Constants: `src/osc/OscConstants.hpp` (ports, timeouts, buffer sizes)

**Creating bundlers (sending data):**
- New bundler: `src/osc/Bundler/MyFeatureBundler.hpp` + `.cpp`
- Base class: `src/osc/Bundler/Bundler.hpp`

**Rendering:**
- Main logic: `src/renderer/Renderer.cpp` (static methods)
- Integration: `src/OSCctrl.cpp` (OSCctrlWidget methods)

**Utilities:**
- Model/widget helpers: `src/util/Util.cpp`
- Network discovery: `src/util/Network.hpp`
- Timers: `src/util/Timer.hpp`

**Build:**
- Makefile: `./Makefile`
- CI/CD: `.github/workflows/build-plugin.yml`

### Workflow 1: Adding a New OSC Endpoint

**Use case:** VR client needs to send a new type of command to VCV Rack.

**Step-by-step:**

1. **Choose OSC path** (follow conventions):
   - `/get/*` - Client requests data
   - `/set/*` - Client sends data (also used for bundler responses)
   - `/subscribe/*` - Real-time streaming
   - `/add/*`, `/remove/*` - Create/delete entities

2. **Add route in `src/osc/OscReceiver.cpp` → `generateRoutes()`:**

```cpp
routes.emplace(
  "/set/param/value",
  [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
    // Extract arguments
    int64_t moduleId = (args++)->AsInt64();
    int32_t paramId = (args++)->AsInt32();
    float value = (args++)->AsFloat();

    // MUST enqueue if touching VCV Rack widgets
    ctrl->enqueueAction([moduleId, paramId, value]() {
      auto mw = APP->scene->rack->getModule(moduleId);
      if (!mw) return;  // Always null-check

      auto param = mw->getParam(paramId);
      if (!param) return;

      param->getParamQuantity()->setValue(value);
    });
  }
);
```

3. **Common patterns:**

**Extract arguments:**
```cpp
int32_t intVal = (args++)->AsInt32();
int64_t longVal = (args++)->AsInt64();
float floatVal = (args++)->AsFloat();
std::string strVal = (args++)->AsString();
```

**Optional arguments:**
```cpp
int64_t optionalId = -1;
try {
  optionalId = (args++)->AsInt64();
} catch (const osc::WrongArgumentTypeException& e) {}
```

**Capture by value in lambda (not reference!):**
```cpp
// ✅ CORRECT
ctrl->enqueueAction([this, moduleId, value]() { ... });

// ❌ WRONG - stack vars invalid when lambda runs
std::string slug = (args++)->AsString();
ctrl->enqueueAction([&slug]() { ... });  // CRASH!
```

4. **If sending response, enqueue bundler:**
```cpp
routes.emplace("/get/module_stubs", [&](auto& args, auto&) {
  ctrl->enqueueAction([this]() {
    osctx->enqueueBundler(new ModuleStubsBundler());
  });
});
```

### Workflow 2: Creating a New Bundler

**Use case:** Need to send structured data to VR client in response to `/get/*` request.

**Step-by-step:**

1. **Create files in `src/osc/Bundler/`:**
   - `MyFeatureBundler.hpp`
   - `MyFeatureBundler.cpp`

2. **Header template:**
```cpp
#pragma once
#include "Bundler.hpp"

struct MyFeatureBundler : Bundler {
  MyFeatureBundler(/* args */);

  // Optional overrides:
  // bool isNoop() override;
  // void sent() override;
  // void done() override;
};
```

3. **Implementation - populate messages in constructor:**

**Pattern A: Simple (no temp widgets):**
```cpp
PatchInfoBundler::PatchInfoBundler(): Bundler("PatchInfoBundler") {
  std::string filename = rack::system::getFilename(APP->patch->path);

  messages.emplace_back(
    "/set/patch_info",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << ctrlId << filename.c_str();
    }
  );
}
```

**Pattern B: Loop over items:**
```cpp
ModuleStubsBundler::ModuleStubsBundler(): Bundler("ModuleStubsBundler") {
  for (const auto& id : APP->engine->getModuleIds()) {
    auto model = APP->engine->getModule(id)->getModel();
    std::string pluginSlug = model->plugin->slug;
    std::string moduleSlug = model->slug;

    messages.emplace_back(
      "/set/module_stub",
      [id, pluginSlug, moduleSlug](osc::OutboundPacketStream& p) {
        p << id << pluginSlug.c_str() << moduleSlug.c_str();
      }
    );
  }
}
```

**Pattern C: Create temp widget for analysis:**
```cpp
ModuleStructureBundler::ModuleStructureBundler(
  const std::string& pluginSlug,
  const std::string& moduleSlug
): Bundler("ModuleStructureBundler") {
  auto model = gtnosft::util::findModel(pluginSlug, moduleSlug);
  auto mw = gtnosft::util::makeConnectedModuleWidget(model);
  if (!mw) return;

  // Analyze widget hierarchy
  addParamMessages(mw);
  addPortMessages(mw);

  delete mw;  // Always cleanup temp widgets
}
```

4. **Use bundler in route handler:**
```cpp
ctrl->enqueueAction([this, pluginSlug, moduleSlug]() {
  osctx->enqueueBundler(new MyFeatureBundler(pluginSlug, moduleSlug));
});
```

### Workflow 3: Modifying Existing Bundlers

**Adding data to existing message:**
```cpp
// In ModuleStubsBundler.cpp
messages.emplace_back(
  "/set/module_stub",
  [id, pluginSlug, moduleSlug, bypassed](auto& p) {  // Add new var
    p << id
      << pluginSlug.c_str()
      << moduleSlug.c_str()
      << bypassed;  // Add to message
  }
);
```

**Adding new message to bundler:**
```cpp
// In constructor, after existing messages
messages.emplace_back(
  "/set/new_data",
  [this, data](osc::OutboundPacketStream& p) {
    p << data;
  }
);
```

### Workflow 4: Common Debugging Scenarios

**Scenario: OSC messages not arriving**

1. Enable RX logging in `src/osc/OscReceiver.cpp` line ~108:
   ```cpp
   DEBUG("oscrx received message on path %s", message.AddressPattern());
   ```
2. Check VCV Rack log: `<VCV Rack user dir>/log.txt`
3. Look for "no route for address" warnings
4. Verify port: Check `OscReceiver::activePort` in log (defaults to 7225)

**Scenario: Bundler not sending**

1. Add log in bundler constructor:
   ```cpp
   INFO("MyBundler created with %d messages", messages.size());
   ```
2. Check if `isNoop()` returning true
3. Verify bundler enqueued in route handler

**Scenario: Crash when accessing widgets**

- **Cause:** Accessing `rack::` objects from OSC thread
- **Fix:** Always use `ctrl->enqueueAction(lambda)` for widget access
- **Check lambda captures:** Capture by value, not reference

**Scenario: Chunked transfer stuck**

1. Enable chunk logging in `src/osc/ChunkedSend/ChunkedSend.cpp`
2. Add log in `/ack_chunk` route to verify ACKs arriving
3. Check client sending ACKs with correct chunkId

**Scenario: Client disconnects**

- Check heartbeat interval: Client must send `/keepalive` every <1000ms
- After 5 missed heartbeats, server switches to broadcast mode
- Verify client heartbeat timing

### Debugging & Logging
- **VCV Rack logging:** Uses `DEBUG()`, `INFO()`, `WARN()` macros from Rack framework
- **Log location:** Check VCV Rack's log.txt in user directory
- **Enable debug output:** Uncomment `DEBUG()` lines in source (many are commented out)
- **Common issues:**
  - Port binding failures: Receiver retries up to 20 times, incrementing port
  - Missed heartbeats: Client switches to broadcast mode after 5 misses
  - Chunked send timeouts: Check acknowledgment messages

## OSC API Documentation

### Connection & Discovery
- `/register` (RX): Register client for direct unicast communication
- `/keepalive` (RX): Client heartbeat to maintain connection
- `/announce` (TX, broadcast): Announces plugin availability with port info
- `/heartbeat` (TX, direct): Sends CPU stats to registered client

### Patch Management
- `/get/patch_info` (RX): Request current patch metadata
- `/set/patch_info` (TX): Returns `ctrlId`, `filename`
- `/patch/open <path>` (RX): Load patch from filesystem

### Module Discovery & Structure
- `/get/module_stubs` (RX): List all modules in rack
- `/set/module_stub` (TX): Module position/identification
- `/get/module_structure <plugin> <module>` (RX): Request UI definition
- `/set/module_structure/*` (TX): Parameter/port/light definitions

### State & Control
- `/get/module_state <moduleId>` (RX): Full module state
- `/get/params_state <moduleId>` (RX): Parameter values only
- `/set/param/value <moduleId> <paramId> <value>` (RX): Set parameter
- `/subscribe/module/lights <moduleId>` (RX): Subscribe to light updates
- `/set/s/m`, `/set/s/p`, `/set/s/l` (TX): State streaming (modules/params/lights)

### Cable Management
- `/get/cables` (RX): List all patch cables
- `/add/cable` (RX): Create cable connection
- `/remove/cable` (RX): Delete cable
- `/set/cable` (TX): Cable definition

### Texture Rendering (Chunked)
- `/get/texture/panel`, `/get/texture/overlay`, `/get/texture/port` (RX)
- `/get/texture/knob`, `/get/texture/switch`, `/get/texture/slider` (RX)
- `/set/texture` (TX): PNG image data in chunks
- `/ack_chunk` (RX): Acknowledge chunk receipt

**Network Configuration:**
- RX Port: 7225 (with retry up to 7244)
- TX Port: 7746
- Max UDP packet: 1452 bytes
- Heartbeat interval: 1000ms
- Max missed heartbeats: 5

## Build Setup & Dependencies

### Prerequisites
- **RACK_DIR:** Path to VCV Rack SDK (defaults to `../..`)
  - Set via environment variable or `make RACK_DIR=/path/to/rack`
  - CI uses VCV Rack SDK v2.6.4
- **Compiler:** C++20 support required
  - Windows: MinGW (detected via `$(CC) -dumpmachine`)
  - Linux: gcc/clang with `-lstdc++`
  - macOS: Clang with CoreFoundation/SystemConfiguration frameworks
- **Build tools:** Make

### Platform-Specific Notes
- **Windows:** Links `-lws2_32 -lwinmm -liphlpapi -lopengl32`
- **macOS:** Supports x64 and arm64 cross-compilation
- **Linux:** Uses POSIX OSC implementation
- **All platforms:** Detects architecture via `arch.mk`

### Build Commands
```bash
make dep          # Build dependencies first
make              # Compile plugin
make dist         # Package for distribution (.vcvplugin)
make clean        # Clean build artifacts
```

### CI/CD Pipeline
- Uses Docker toolchain: `ghcr.io/qno/rack-plugin-toolchain-win-linux`
- macOS builds on native runners
- Automated builds for Windows, Linux, Mac
- Output: `.vcvplugin` files in `dist/`

## Common Gotchas

### Thread Safety
- ❌ **NEVER access `rack::` objects from OSC threads** - Always use `ctrl->enqueueAction()`
- ❌ **NEVER capture by reference in enqueued lambdas** - Stack vars will be invalid
  ```cpp
  // WRONG:
  std::string slug = (args++)->AsString();
  ctrl->enqueueAction([&slug]() { ... });  // CRASH!

  // CORRECT:
  std::string slug = (args++)->AsString();
  ctrl->enqueueAction([slug]() { ... });  // Safe - copied
  ```

### Component Initialization
- **Order matters:** sender → chunkman → subman → receiver
- Don't swap initialization order in `OSCctrlWidget` constructor

### Widget Lifecycle
- **Widgets can be destroyed during patch operations** - Always null-check
  ```cpp
  auto mw = APP->scene->rack->getModule(id);
  if (!mw) return;  // ALWAYS check
  ```
- **Temp widgets must be deleted** - In bundlers that create widgets for analysis
  ```cpp
  auto mw = gtnosft::util::makeConnectedModuleWidget(model);
  // ... use widget ...
  delete mw;  // Don't forget!
  ```

### OSC Protocol
- **Packet size limit:** 1452 bytes max per UDP packet
  - Large bundlers automatically split across multiple sends
  - Messages >1452 bytes are skipped (caught by exception)
- **Chunked transfers require ACK:** Client must send `/ack_chunk` or transfer stalls
  - Server waits for ACK before sending next chunk
  - No ACK = timeout and failure
- **String arguments:** Use `.c_str()` for `std::string`
  ```cpp
  pstream << myString.c_str();  // Correct
  pstream << myString;          // Wrong - won't compile
  ```

### Rendering
- **SVG rendering requires framebuffer** - Fallback to widget rendering if unavailable
- **OpenGL context required** - Rendering must happen on main thread (automatically handled by enqueueAction)
- **Vertical flip needed** - OpenGL Y-up vs image Y-down coordinate systems

### Network
- **Port binding:** If port 7225 in use, receiver auto-increments up to 7244
  - Check logs for actual port: "OSCctrl started OSC receiver on port XXXX"
- **Broadcast address calculation:** Relies on network adapter detection
  - May fail on complex network setups (VPNs, virtual adapters)
  - Logs warning if broadcast address can't be determined
- **Heartbeat timing:**
  - Server expects client `/keepalive` every <1000ms
  - Client should send every ~500-750ms for safety margin
  - 5 missed heartbeats → switches to broadcast mode

### Development
- **Makefile dependency:** Run `make dep` before first build
- **RACK_DIR must be set** - Points to VCV Rack SDK (defaults to `../..`)
- **Commented DEBUG logs** - Many debug logs are commented out for production
  - Uncomment in source files to enable detailed logging
  - Don't commit uncommented DEBUG logs

## References
- [VCV Rack development docs](https://vcvrack.com/manual/Building)
- [VCV Rack API documentation](https://vcvrack.com/docs-v2/)
    - VCV Rack repo location is stored in RACK_DIR env variable
- [Plugin repository](https://github.com/gotno/gtnosft-rack)
- [oscpack documentation](https://www.rossbencina.com/code/oscpack)
- [qoi documentation](https://qoiformat.org/)
```
