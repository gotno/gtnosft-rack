```markdown
# Copilot Instructions for gtnosft-rack

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

### Naming Patterns
- **Classes/Structs:** PascalCase (`OscSender`, `ModuleParamsBundler`, `OSCctrlWidget`)
- **Methods:** camelCase (`enqueueBundler()`, `setBroadcasting()`, `process()`)
- **Variables:** camelCase (`osctx`, `chunkman`, `messageWidgets`)
- **Bundler naming:** `[Data]*Bundler` pattern (e.g., `ModuleStateBundler`, `PatchInfoBundler`)

### File Organization
- Header/implementation separation: `.hpp` for declarations, `.cpp` for definitions
- Use forward declarations in headers to minimize includes

### Design Patterns
- **Bundler pattern:** Abstract base class (`Bundler`) with virtual `isNoop()`, `sent()`, `done()` methods
- **Manager pattern:** Lifecycle management (e.g., `ChunkedManager`, `SubscriptionManager`)
- **Queue-based async:** Action queues with mutex protection for thread safety
- **Widget composition:** `OSCctrlWidget` composes sender/receiver/managers

## Development Workflow

### Adding a New OSC Endpoint
1. Define the OSC path and Add message handler in `src/osc/OscReceiver.cpp` → `generateRoutes()`
2. Create bundler in `src/osc/Bundler/` if sending data back
3. Update OSC API documentation section below

### Debugging & Logging
- **VCV Rack logging:** Uses `DEBUG()`, `INFO()`, `WARN()` macros from Rack framework
- **Log location:** Check VCV Rack's log.txt in user directory
- **Common issues:**
  - Port binding failures: Receiver retries up to 20 times, incrementing port
  - Missed heartbeats: Client switches to broadcast mode after 5 misses
  - Chunked send timeouts: Check acknowledgment messages

### Thread Safety Considerations
- OSC receiver runs on dedicated `listenerThread`
- Heartbeat runs on `heartbeatThread`
- Queue processing uses `queueWorker` thread
- Always protect shared state with `actionMutex`
- Use `actionQueue` pattern for cross-thread communication

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
- **OSC packet size limit:** Messages split into multiple bundles if >1452 bytes
- **Chunked image acknowledgment:** Client must ACK each chunk or transfer stalls
- **Module lifecycle:** Widgets can be destroyed during patch operations; use null checks
- **SVG rendering:** Requires valid framebuffer; fallback to widget rendering if unavailable
- **Port binding:** If port 7225 in use, receiver auto-increments (check logs for actual port)
- **Thread coordination:** Never modify Rack state directly from OSC thread; use actionQueue

## References
- [VCV Rack development docs](https://vcvrack.com/manual/Building)
- [VCV Rack API documentation](https://vcvrack.com/docs-v2/)
    - VCV Rack repo location is stored in RACK_DIR env variable
- [Plugin repository](https://github.com/gotno/gtnosft-rack)
- [oscpack documentation](https://www.rossbencina.com/code/oscpack)
- [qoi documentation](https://qoiformat.org/)
```
