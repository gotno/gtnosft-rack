# Architecture

**Analysis Date:** 2025-01-06

## Pattern Overview

**Overall:** Publisher-Subscriber with Queued Message Streaming

**Key Characteristics:**
- **Event-driven:** Central widget (`OSCctrlWidget`) coordinates among multiple subsystems
- **Thread-separated:** Three independent thread pools: main UI thread, OSC listener thread (blocking UDP), OSC sender thread (queue worker)
- **Bundler abstraction:** Pluggable message builders serialize rack state to OSC packets
- **Chunked transfers:** Large payloads (images) split across multiple UDP packets with acknowledgment-based retransmission
- **Heartbeat-based mode switching:** Automatic broadcast ↔ direct unicast switching based on client connectivity

## Layers

**Coordination Layer:**
- Purpose: Central widget managing lifecycle and thread-safe action dispatch
- Location: `src/OSCctrl.hpp`, `src/OSCctrl.cpp`
- Contains: `OSCctrlWidget` (ModuleWidget subclass), `OSCctrl` (empty Module), `SceneAction` (deferred action executor)
- Depends on: OscSender, OscReceiver, ChunkedManager, SubscriptionManager
- Used by: VCV Rack plugin system (instantiation), other subsystems (action queueing)

**OSC Send Layer:**
- Purpose: Serialize and transmit OSC messages via UDP with queue-based threading
- Location: `src/osc/OscSender.hpp`, `src/osc/OscSender.cpp`
- Contains: `OscSender` (message queue, sender thread, broadcast/direct endpoint management)
- Depends on: Bundler (abstract), oscpack (UDP socket library)
- Used by: ChunkedManager, SubscriptionManager, heartbeat mechanisms

**OSC Receive & Route Layer:**
- Purpose: Listen for incoming OSC messages and dispatch to handlers
- Location: `src/osc/OscReceiver.hpp`, `src/osc/OscReceiver.cpp`
- Contains: `OscReceiver` (listener socket, message routing, heartbeat monitoring)
- Depends on: OscSender (for heartbeat responses), ChunkedManager, SubscriptionManager, oscpack
- Used by: Coordination layer

**Message Bundling Layer (Abstract Strategy):**
- Purpose: Provide pluggable strategies for serializing VCV Rack state to OSC messages
- Location: `src/osc/Bundler/Bundler.hpp` (abstract base), concrete implementations in `src/osc/Bundler/`
- Contains: Abstract `Bundler` base class with concrete implementations:
  - `ModuleStateBundler` - module position, overlay texture ID
  - `ModuleParamsBundler` - parameter values, labels, visibility
  - `ModuleLightsBundler` - light colors, visibility
  - `ModuleStubsBundler`, `ModuleStructureBundler`, `CablesBundler` - rack topology
  - `PatchInfoBundler` - global patch metadata
  - `BroadcastHeartbeatBundler`, `DirectHeartbeatBundler` - connectivity keepalives
  - `ChunkedImageBundler` - image chunk fragments
- Depends on: rack.hpp (module/widget access), osc::OutboundPacketStream
- Used by: OscSender (queue), ChunkedManager (image transmission)

**Chunked Transfer Layer:**
- Purpose: Split large payloads into MTU-size chunks with retry logic
- Location: `src/osc/ChunkedSend/ChunkedSend.hpp`, `src/osc/ChunkedSend/ChunkedImage.hpp`, `src/osc/ChunkedManager.hpp`
- Contains: 
  - `ChunkedSend` (abstract, tracks chunk ACK status and retry counts)
  - `ChunkedImage` (concrete, compresses pixel data with QOI, inherits from ChunkedSend)
  - `ChunkedManager` (lifecycle: add → process → ack/timeout → cleanup)
- Depends on: qoi library (image codec), ChunkedImageBundler
- Used by: OscReceiver (ACK handling), rendering pipeline

**Subscription Management Layer:**
- Purpose: Poll and send state changes for subscribed modules at regular intervals
- Location: `src/osc/SubscriptionManager.hpp`, `src/osc/SubscriptionManager.cpp`
- Contains: `SubscriptionManager` (subscription set, send interval timer, in-flight tracking)
- Depends on: Timer utility, ModuleLightsBundler
- Used by: OscReceiver (subscription commands), Coordination layer

**Rendering Layer:**
- Purpose: Render VCV Rack widgets to pixel buffers for transmission
- Location: `src/texture/Renderer.hpp`, `src/texture/Renderer.cpp`, `src/texture/Catalog.hpp`, `src/texture/Catalog.cpp`
- Contains: 
  - `Renderer` (converts widgets to PNG pixels using VCV's framebuffer)
  - `Catalog` (caches rendered overlays, deduplicates texture transmission)
  - `Recipe` (specification for scaled vs. exact-size renders)
  - `RenderResult` (success/failure with diagnostic messages)
- Depends on: VCV Rack rendering pipeline, OpenGL framebuffers
- Used by: Bundlers, rendering-related OSC routes

**Utilities:**
- `Timer`/`Interval` (`src/util/Timer.hpp`): Promise-based recurring timers with cancellation
- `Network` (`src/util/Network.hpp`): Broadcast address discovery
- Constants (`src/osc/OscConstants.hpp`): Buffer sizes, ports, timing constants

## Data Flow

**Initial Startup:**
1. Plugin system creates `OSCctrl` (empty module) and `OSCctrlWidget`
2. OSCctrlWidget constructor instantiates `OscSender` → `ChunkedManager` → `SubscriptionManager` → `OscReceiver`
3. OscSender starts queue worker thread
4. OscReceiver starts listener thread (binds to RX_PORT 7225, auto-retries if port taken)
5. OscReceiver starts heartbeat timer (1000ms interval)

**Broadcast Discovery (Initial State):**
1. OscSender starts in broadcast mode to network's calculated broadcast address (TX_PORT 7746)
2. Heartbeat bundler enqueued every 1000ms to discover clients
3. Listener thread blocks on UDP socket, waiting for client messages

**Client Direct Connection:**
1. Remote client sends `/heartbeat` (or any message) with source IP in UDP packet
2. OscReceiver.ProcessMessage routes to heartbeat handler
3. Handler extracts remote IP, calls `OscSender.setDirect(ip)`
4. OscSender switches to direct unicast mode
5. Heartbeat resets missed counter

**State Subscription (e.g., lights):**
1. Remote client sends `/subscribe/lights` with module IDs
2. OscReceiver routes to handler, calls `SubscriptionManager.subscribeModuleLights(moduleId)`
3. Timer-based `SubscriptionManager.tick()` fires every 30ms
4. Creates `ModuleLightsBundler`, enqueues to OscSender
5. OscSender worker thread pops bundler, iterates messages
6. Each message fits into 1452-byte MTU, gets sent as atomic UDP packet
7. Light state cache prevents redundant messages

**Image Transmission (Chunked):**
1. Command or code requests panel render of module widget
2. Renderer captures pixels to buffer (RGBA, 4 bytes per pixel)
3. ChunkedImage compresses with QOI codec
4. ChunkedManager.add() queues for transmission
5. ChunkedManager.processChunked() generates ChunkedImageBundler for each chunk
6. Each bundler specifies chunk offset/size, bundler enqueued to OscSender
7. Remote client ACKs each chunk via `/ack` message
8. OscReceiver routes to ChunkedManager.ack()
9. On chunk timeout (200ms poll, MAX_SENDS=5), bundler re-enqueued
10. On all chunks ACKed, ChunkedManager removes from active set
11. Deferred sends (queued while transmission in progress) auto-retry

**Subscription Reset (Client Disconnect):**
1. Heartbeat timer triggers every 1000ms
2. No `/heartbeat` received → missedHeartbeats increments
3. After 5 misses (5 seconds), OscReceiver switches back to broadcast
4. SubscriptionManager.reset() clears all subscriptions and caches

## Key Abstractions

**Bundler (Abstract Strategy Pattern):**
- Purpose: Pluggable serialization of rack state to OSC packets
- Pattern: 
  - Subclasses populate `std::vector<message>` in constructor
  - Each message = (OSC path, lambda that writes args to stream)
  - `bundle()` iterates messages until buffer full or all consumed
  - `isNoop()` / `sent()` / `done()` lifecycle hooks
- Examples:
  - `ModuleStateBundler`: single message with module ID, position, texture
  - `ModuleParamsBundler`: one message per changed param, static cache across instances
  - `ChunkedImageBundler`: sends single chunk with metadata (id, chunk#, width, height)

**ChunkedSend (Abstract Lifecycle):**
- Purpose: Track status and retransmission of large transfers
- Pattern:
  - Tracks chunk-level ACK timestamps, send times, send counts
  - `sendFailed()`: true if any chunk exceeded MAX_SENDS (5)
  - `sendSucceeded()`: true if all chunks ACKed
  - Virtual `getBundlerForChunk(int)` returns bundler for retransmission
- Concrete: `ChunkedImage` implements QOI compression, image-specific bundler

**SubscriptionManager (Polling Dispatcher):**
- Purpose: Periodically send deltas for subscribed modules
- Pattern:
  - `inFlight` map tracks which subscription types are awaiting ACK/completion
  - `tick()` fires if subscription exists and previous bundler completed
  - Bundler.onBundleSent callback sets `inFlight[type] = false`
  - Prevents concurrent sends of same type

## Entry Points

**Plugin Entry:**
- Location: `src/plugin.cpp`
- Triggers: VCV Rack plugin system on load
- Responsibilities: Register `modelOSCctrl` with plugin

**Widget Constructor:**
- Location: `src/OSCctrl.cpp`: `OSCctrlWidget::OSCctrlWidget(OSCctrl* module)`
- Triggers: Rack creates module widget when instantiated
- Responsibilities: Create OscSender, ChunkedManager, SubscriptionManager, OscReceiver; set panel SVG

**Main Thread step() Method:**
- Location: `src/OSCctrl.cpp`: `OSCctrlWidget::step()`
- Triggers: Rack calls on main thread every frame (~60Hz)
- Responsibilities: Process action queue from listener/sender threads

**OSC Listener Thread:**
- Location: `src/osc/OscReceiver.cpp`: `OscReceiver::startListener()`
- Triggers: Constructor, blocks on UDP socket
- Responsibilities: Parse incoming OSC messages, route to handlers, enqueue render/state actions

**OSC Sender Thread:**
- Location: `src/osc/OscSender.cpp`: `OscSender::processQueue()`
- Triggers: Constructor, waits on condition variable
- Responsibilities: Pop bundlers from queue, serialize messages, send UDP packets

## Error Handling

**Strategy:** Non-fatal logging with graceful degradation

**Patterns:**
- **Port binding failure:** OscReceiver retries on alternate ports (up to 20 attempts from RX_PORT 7225)
- **Network send failure:** OscSender catches exception, logs warning with endpoint/mode, continues
- **Chunk send timeout:** Chunk re-enqueued up to 5 times; after 5th failure, transfers marked as `failed`
- **Message too large:** Bundler skips individual message if it exceeds buffer after serialization; logs warning with path
- **Missing module:** Bundler checks APP->scene->rack->getModule() before accessing; silently skips if gone
- **Render failure:** Renderer returns RenderResult with Failure status and message; caller decides action
- **Timer exceptions:** Interval::work() catches all exceptions, logs warning, terminates timer thread

## Cross-Cutting Concerns

**Thread Safety:**
- `actionQueue` (OSCctrlWidget) protected by mutex; main thread drains, worker threads enqueue
- `bundlerQueue` (OscSender) protected by mutex + condition variable; owned by worker thread
- `chunkedSends` (ChunkedManager) accessed from main thread (ACK handler) and detached threads (processing); uses unique_ptr but not lock-protected (assumes short-lived access)
- `inFlight` (SubscriptionManager) atomic<bool> per subscription type
- Chunk-level ACK status (ChunkedSend) protected by mutex; shared between main thread (ProcessMessage) and detached timer thread (processChunked)

**Logging:**
- Framework: VCV Rack's rack.hpp macros (WARN, INFO, DEBUG)
- Locations: OscSender (send errors), OscReceiver (bind errors, heartbeat), ChunkedManager (processing flow), Interval (exceptions)
- No logs in hot paths (bundler loop, ack checking)

**Validation:**
- Network input: OSC paths validated against route map; unmapped paths rejected with exception
- State access: Modules/widgets checked for null before dereference
- Buffers: MSG_BUFFER_SIZE (1452 bytes) and EMPTY_BUNDLE_SIZE (16 bytes) constants enforce envelope checks
- Timing: Heartbeat delay constants (HEARTBEAT_DELAY, SUBSCRIPTION_SEND_DELAY, chunk retry 200ms) tuned to UDP/network stability

---

*Architecture analysis: 2025-01-06*
