# Architecture

## Core Sections (Required)

### 1) Architectural Style

- **Primary style**: Event-driven with thread-boundary marshalling, layered within VCV Rack's plugin model
- **Why this classification**: OSC messages arrive on a background listener thread; all Rack API access must happen on the render thread. The dominant structural concern is safely crossing this boundary via an action queue. The `Bundler` command pattern separates data collection from transmission.
- **Primary constraints**:
  1. Rack API (modules, widgets, cables, params) is only safe to call on the render/UI thread
  2. OSC networking is blocking/threaded and must not block the render loop
  3. Large payloads (rendered textures) exceed a single UDP packet and require a reliable chunked protocol

### 2) System Flow

#### Inbound (OSC → Rack)

```text
UDP packet (client)
  → oscpack listener thread (OscReceiver::ProcessMessage)
    → route dispatch (routes map, keyed by OSC address)
      → ctrl->enqueueAction(lambda)  ← thread boundary
        → render thread: OSCctrlWidget::processActionQueue (called each step())
          → Rack API calls (set param, add cable, render texture, etc.)
            → [optional] osctx->enqueueBundler(new XxxBundler())
              → OscSender queue worker thread
                → UDP packet (to client)
```

#### Outbound subscription (periodic)

```text
Interval thread (SubscriptionManager::tick, every 30 ms)
  → ctrl->enqueueAction(lambda)  ← thread boundary
    → render thread: new ModuleLightsBundler(moduleIds, callback)
      → osctx->enqueueBundler(bundler)
        → OscSender queue worker thread
          → UDP packet (to client)
```

#### Texture / chunked send

```text
Render thread: /get/texture handler
  → Catalog::pullTexture → Renderer::renderTexture
    → ChunkedImage (compress with qoi)
      → ChunkedManager::add
        → ChunkedSendBundler per chunk → osctx->enqueueBundler
          → OscSender sends chunk
            → client sends /ack_chunk
              → ChunkedManager::ack → re-process remaining chunks
```

### 3) Layer/Module Responsibilities

| Layer or module | Owns | Must not own | Evidence |
|-----------------|------|--------------|----------|
| `OSCctrlWidget` | Object lifecycle, render-thread action queue (`actionQueue`), `step()` loop | OSC parsing, data collection, networking | `src/OSCctrl.cpp` |
| `OscReceiver` | UDP listening thread, route dispatch, heartbeat tracking | Direct Rack API calls | `src/osc/OscReceiver.cpp` |
| `OscSender` | UDP send socket, broadcast/direct mode, background queue worker | Data collection, routing | `src/osc/OscSender.cpp` |
| `Bundler` subclasses | Collecting Rack state and encoding OSC messages | Sending, routing, subscriptions | `src/osc/Bundler/` |
| `SubscriptionManager` | Managing light subscriptions, firing periodic sends | Rendering, chunking, routing | `src/osc/SubscriptionManager.cpp` |
| `ChunkedManager` | Reliable multi-chunk send lifecycle (ack tracking, defer, retry) | OSC encoding, Rack API | `src/osc/ChunkedManager.cpp` |
| `Catalog` | Assigning and caching texture IDs via rapidhash | Rendering, networking | `src/texture/Catalog.cpp` |
| `Renderer` | Off-screen framebuffer rendering, pixel readback, scale calculation | Networking, ID assignment | `src/texture/Renderer.cpp` |
| `util/` | Timer/Interval, network adapter enumeration, Rack helper functions | Domain logic | `src/util/` |

### 4) Reused Patterns

| Pattern | Where found | Why it exists |
|---------|-------------|---------------|
| **Command (Bundler)** | `src/osc/Bundler/` — one subclass per message type | Decouples data collection (render thread) from transmission (sender thread); enables multi-message pagination |
| **Action queue** | `OSCctrlWidget::actionQueue` + `enqueueAction` / `processActionQueue` | Thread-safe bridge from any background thread back to the Rack render thread |
| **SceneAction (self-destructing widget)** | `src/OSCctrl.hpp` `SceneAction::Create` | Forces execution on the deepest Rack scene step; used for operations that must happen in the scene graph context |
| **Interval/Timer** | `src/util/Timer.hpp` | JS-style `setInterval`/`setTimeout` using `std::promise` + `std::thread`; used for heartbeat and subscription ticks |
| **Class-level singleton maps** | `ModuleLightsBundler::lights`, `ModuleParamsBundler::params`, `Catalog::registry` | Persistent cross-call state caches; implemented as `inline static` members |
| **Chunked reliable transfer** | `ChunkedSend` + `ChunkedManager` | Custom ACK protocol to reliably send payloads too large for a single UDP packet |

### 5) Known Architectural Risks

- **No Rack undo history for cable mutations**: `/add/cable` and `/remove/cable` bypass the undo stack (explicit TODOs in `OscReceiver.cpp:367,404`). If a user triggers undo after remote cable operations, results are unpredictable.
- **Detached threads in ChunkedManager**: `processChunked` spawns a `std::thread([…]).detach()` for the retry delay (`ChunkedManager.cpp:105`). These threads hold captures referencing `this` with no lifecycle guarantee if the plugin is torn down mid-transfer.
- **Global mutable class state**: `inline static` maps in `ModuleLightsBundler` and `Catalog` are never reset between patch loads, which can result in stale texture IDs or light state after a patch is changed.
- **No authentication/access control**: Any host reachable on the LAN can send OSC commands to modify the running patch (set params, add/remove cables, open patch files).

### 6) Evidence

- `src/OSCctrl.cpp` (action queue, step loop)
- `src/OSCctrl.hpp` (SceneAction)
- `src/osc/OscReceiver.cpp` (route dispatch, thread boundary crossings)
- `src/osc/OscSender.cpp` (queue worker)
- `src/osc/SubscriptionManager.cpp` (periodic subscription tick)
- `src/osc/ChunkedManager.cpp` (chunked transfer lifecycle)
- `src/texture/Catalog.hpp`, `src/texture/Renderer.hpp`
- `src/util/Timer.hpp`
