# Codebase Structure

## Core Sections (Required)

### 1) Top-Level Map

| Path | Purpose | Evidence |
|------|---------|----------|
| `src/` | All plugin source code | `Makefile` (`SOURCES`) |
| `src/OSCctrl.cpp` / `src/OSCctrl.hpp` | Main VCV Rack module struct and widget; owns all runtime objects | `src/OSCctrl.cpp` |
| `src/plugin.cpp` / `src/plugin.hpp` | Plugin registration; declares `pluginInstance` and `modelOSCctrl` | `src/plugin.cpp`, `src/plugin.hpp` |
| `src/osc/` | OSC networking: receiver, sender, routing, subscriptions, chunked sends | `src/osc/OscReceiver.cpp` |
| `src/osc/Bundler/` | One `Bundler` subclass per outbound message type | `src/osc/Bundler/` |
| `src/osc/ChunkedSend/` | Reliable fragmented transfer of large payloads (images) | `src/osc/ChunkedSend/ChunkedSend.hpp` |
| `src/texture/` | Module widget rendering pipeline: `Catalog` (ID registry) + `Renderer` (framebuffer capture) | `src/texture/Catalog.hpp`, `src/texture/Renderer.hpp` |
| `src/util/` | Shared utilities: `Timer` (interval/timeout), `Network` (broadcast address), `Util` (helpers) | `src/util/` |
| `dependencies/` | Vendored C/C++ libraries (oscpack, qoi, rapidhash, stb_image_write) | `dependencies/` |
| `res/` | SVG panel artwork for the OSCctrl module UI | `res/OSCctrl.svg` |
| `plugin.json` | VCV Rack plugin manifest (slug, version, module list) | `plugin.json` |
| `Makefile` | Build system — sources, flags, distributables | `Makefile` |
| `.github/workflows/` | CI/CD pipeline (build + publish) | `.github/workflows/build-plugin.yml` |
| `build-wsl/`, `build-msys/` | Build artefact directories for local WSL/MSYS2 builds | `build-wsl.sh`, `build-msys.sh` |
| `build/` | Default build artefact directory | `Makefile` |
| `dist/` | Distribution package output (`.vcvplugin`) | `Makefile` |

### 2) Entry Points

- **Plugin load entry**: `src/plugin.cpp` — `init(Plugin* p)` registers `modelOSCctrl` with VCV Rack
- **Module instantiation entry**: `src/OSCctrl.cpp` — `OSCctrlWidget::OSCctrlWidget(OSCctrl*)` constructs all runtime objects (`OscSender`, `OscReceiver`, `ChunkedManager`, `SubscriptionManager`)
- **OSC message entry**: `OscReceiver::ProcessMessage()` — called by oscpack on the listener thread
- No CLI or worker entry points; the plugin runs entirely inside VCV Rack's process

### 3) Module Boundaries

| Boundary | What belongs here | What must not be here |
|----------|-------------------|------------------------|
| `src/OSCctrl.cpp` | Module/widget lifecycle, render-thread action queue | OSC parsing, networking, texture logic |
| `src/osc/OscReceiver.cpp` | Route dispatch, OSC message parsing, heartbeat monitoring | Direct Rack API calls (must use `enqueueAction`) |
| `src/osc/OscSender.cpp` | UDP socket management, message queue worker, send modes | Data collection from Rack state |
| `src/osc/Bundler/` | Collecting Rack state and encoding it into OSC messages | Sending logic (delegated to `OscSender`) |
| `src/texture/Renderer.cpp` | Framebuffer setup, NanoVG rendering, pixel readback | Network/OSC concerns |
| `src/texture/Catalog.cpp` | Texture ID assignment and deduplication via rapidhash | Rendering logic |
| `src/util/` | Generic, stateless helpers | Plugin-specific logic |

### 4) Naming and Organization Rules

- **File naming**: PascalCase — `OscReceiver.cpp`, `ChunkedManager.hpp`, `ModuleLightsBundler.cpp`
- **Directory organization**: by feature/subsystem (`osc/`, `texture/`, `util/`); further by type within `osc/` (`Bundler/`, `ChunkedSend/`)
- **Include paths**: relative from `src/` (e.g., `#include "../texture/Catalog.hpp"` from within `src/osc/`); vendored deps included via `-I./dependencies` flag so `oscpack/` headers are available as `#include "oscpack/…"`
- No path aliases or barrel headers

### 5) Evidence

- `Makefile` (source glob patterns)
- `src/plugin.cpp`, `src/plugin.hpp`
- `src/OSCctrl.cpp`, `src/OSCctrl.hpp`
- `src/osc/OscReceiver.cpp`
- `src/texture/Catalog.hpp`
