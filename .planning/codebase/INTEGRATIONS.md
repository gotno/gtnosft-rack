# External Integrations

**Analysis Date:** 2025-01-15

## APIs & External Services

**OSC (Open Sound Control) Protocol:**
- UDP-based network protocol for real-time communication with DAWs and control software
  - SDK/Client: `oscpack` library (vendored in `dependencies/oscpack/`)
  - Communication: Bidirectional OSC messages via UDP
  - Implementation files: `src/osc/OscSender.hpp`, `src/osc/OscReceiver.hpp`

## Network Communication

**Broadcast Mode:**
- Default operation mode for OSC sender
- Sends OSC bundles to calculated broadcast address on local network
- Broadcast address calculated from active network adapter IP and netmask
- See: `src/osc/OscSender.cpp::OscSender()` constructor

**Direct Mode:**
- Allows sending OSC messages to specific IP addresses
- API: `OscSender::setDirect(char* ip)`
- Overrides broadcast endpoint with direct target

**Network Adapter Detection:**
- Implementation: `src/util/Network.hpp`
- Detects all connected network adapters (Ethernet and WiFi)
- Prefers WiFi, falls back to Ethernet, then any connected adapter
- Platform-specific implementation:
  - Windows: Uses `GetAdaptersAddresses()` API (requires `iphlpapi.h`)
  - Linux/macOS: Uses `getifaddrs()` POSIX function

## OSC Ports

**Configuration:** `src/osc/OscConstants.hpp`
- RX_PORT: 7225 (RACK) - Receives OSC messages from external controllers
- TX_PORT: 7746 (RSIM) - Sends OSC messages (broadcast or direct)
- MSG_BUFFER_SIZE: 1452 bytes (UDP best-case MTU)
- HEARTBEAT_DELAY: 1000ms - Heartbeat interval for connection monitoring
- HEARTBEAT_INTERVAL: 5 missed heartbeats before timeout
- SUBSCRIPTION_SEND_DELAY: 30ms - Rate limit for subscription updates

## Message Types

**OSC Bundles:**
- All messages sent as immediate OSC bundles
- Implementation: `src/osc/Bundler/Bundler.hpp` and subclasses
- Bundle types managed via pluggable `Bundler` interface

**Message Categories (via bundler implementations):**
- `PatchInfoBundler` - VCV Rack patch metadata
- `ModuleStubsBundler` - Module identification and stub information
- `ModuleStructureBundler` - Module parameter/port structure definitions
- `ModuleStateBundler` - Module parameter values and state
- `ModuleParamsBundler` - Individual parameter updates
- `CablesBundler` - Patch cable connections
- `DirectHeartbeatBundler` - Direct-mode heartbeat messages
- `BroadcastHeartbeatBundler` - Broadcast-mode heartbeat messages
- `ChunkedImageBundler` - Large image data fragmented into multiple OSC messages
- `ChunkedSendBundler` - Generic chunked message sending

**Chunked Message Handling:**
- Images and large data split into chunks for transmission (UDP packet size constraints)
- Implementation: `src/osc/ChunkedSend/ChunkedImage.hpp`, `src/osc/ChunkedSend/ChunkedManager.hpp`
- Receiver reassembles chunked messages
- See: `src/osc/ChunkedManager.hpp`

## Subscription System

**Request/Response Pattern:**
- External clients subscribe to module/parameter updates via OSC messages
- Implementation: `src/osc/SubscriptionManager.hpp`, `src/osc/SubscriptionManager.cpp`
- Tracks subscription requests and pushes updates at throttled rate
- Respects SUBSCRIPTION_SEND_DELAY (30ms) to avoid overwhelming network

## VCV Rack Framework Integration

**Plugin Interface:**
- Entry point: `src/plugin.cpp::init()` - Called by Rack to initialize plugin
- Module registration: `plugin->addModel(modelOSCctrl)` - Registers OSC module
- Module definition: `src/OSCctrl.hpp`, `src/OSCctrl.cpp` - OSCctrl module implementation

**Widget System:**
- `OSCctrlWidget` - Main UI widget for OSC control module
- Location: `src/OSCctrl.hpp`
- Inherits from `rack::app::ModuleWidget`
- Manages OSC sender, receiver, chunking, and subscription managers

**Module Callbacks:**
- `OSCctrl::process()` - Called by Rack audio thread each sample (currently unused)
- `OSCctrlWidget::step()` - Called by Rack UI thread each frame for action queue processing

**Render Thread Synchronization:**
- Thread-safe action queuing system for operations that must run on Rack's render thread
- Implementation: `OSCctrlWidget::actionQueue`, `OSCctrlWidget::enqueueAction()`, `OSCctrlWidget::processActionQueue()`
- Uses `std::mutex` for synchronization

## Texture/Asset System

**Texture Rendering:**
- Module includes image rendering pipeline for texture management
- Files: `src/texture/Renderer.hpp`, `src/texture/Renderer.cpp`, `src/texture/Catalog.hpp`, `src/texture/Catalog.cpp`
- Uses rapidhash for content addressing (cache keys)
- Uses QOI format for efficient image compression (unused STB image encoding available)

**Asset Loading:**
- VCV Rack asset system: `asset::plugin(pluginInstance, "res/OSCctrl.svg")` - Panel SVG
- UI panel: `res/OSCctrl.svg`

## Heartbeat/Keepalive

**Connection Monitoring:**
- OSC receiver tracks heartbeat from sender
- Timeout after 5 missed heartbeats (MAX_MISSED_HEARTBEATS)
- Implementation: `src/osc/OscReceiver.hpp` lines 61-66
- Used to detect and respond to disconnections

## Message Processing Pipeline

**Receive Pipeline:**
1. `OscReceiver::ProcessMessage()` - Receives raw OSC packets on listening thread
2. Routes messages via generated routing table (see `OscReceiver::generateRoutes()`)
3. Dispatches to appropriate handler (subscription response, state updates, etc.)
4. Actions enqueued for execution on Rack render thread via `OSCctrlWidget::enqueueAction()`

**Send Pipeline:**
1. Various `Bundler` implementations build OSC messages
2. Bundlers enqueued via `OscSender::enqueueBundler()`
3. Queue worker thread (started in `OscSender::startQueueWorker()`) processes bundlers
4. Messages sent via broadcast or direct endpoint using oscpack `UdpSocket`

## Thread Model

**UDP Listening Thread:**
- Runs OSC receiver: `std::thread` created in `OscReceiver::startListener()`
- Blocks on `UdpListeningReceiveSocket::Run()` to receive OSC packets
- Calls `ProcessMessage()` on received packets
- Port binding with retry logic (up to 20 attempts)

**Message Queue Worker Thread:**
- Runs message sender: `std::thread queueWorker` in `OscSender`
- Waits on `std::condition_variable` for bundlers to send
- Processes queue items and sends via UDP socket
- Condition variable signaled when bundlers enqueued

**Render Thread:**
- Rack's main UI thread calls `OSCctrlWidget::step()` each frame
- Processes queued actions via `processActionQueue()` with mutex protection

---

*Integration audit: 2025-01-15*
