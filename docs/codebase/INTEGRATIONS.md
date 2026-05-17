# External Integrations

## Core Sections (Required)

### 1) Integration Inventory

| System | Type | Purpose | Auth model | Criticality | Evidence |
|--------|------|---------|------------|-------------|----------|
| VCV Rack engine API | In-process C++ API | Read/write module state, params, cables, patch | None (in-process) | High — plugin cannot function without it | `src/osc/OscReceiver.cpp`, `src/osc/Bundler/` |
| OSC client (remote) | UDP / OSC protocol | Command source and telemetry sink; the "user" of this plugin | None | High — the entire purpose of the plugin | `src/osc/OscReceiver.cpp`, `src/osc/OscSender.cpp` |
| Local network | UDP broadcast + unicast | Discovery (broadcast) and direct communication (unicast after `/register`) | None | High | `src/osc/OscSender.cpp`, `src/util/Network.hpp` |

### 2) Data Stores

No databases or persistent data stores. All state is in-memory and is lost when the plugin is unloaded.

| Store | Role | Access layer | Key risk | Evidence |
|-------|------|--------------|----------|----------|
| `Catalog::registry` (inline static map) | Texture ID ↔ Breadcrumbs mapping | `src/texture/Catalog.cpp` | Never reset on patch change; stale IDs possible across patches | `src/texture/Catalog.hpp` |
| `ModuleLightsBundler::lights` (inline static map) | Per-module light state cache (dirty tracking) | `src/osc/Bundler/ModuleLightsBundler.cpp` | Cleared only on client disconnect; stale if module is removed without disconnect | `src/osc/Bundler/ModuleLightsBundler.hpp` |
| `ModuleParamsBundler::params` (inline static map) | Per-module param state cache (dirty tracking) | `src/osc/Bundler/ModuleParamsBundler.cpp` | Same risk as lights cache | `src/osc/SubscriptionManager.cpp` (reset call) |

### 3) Secrets and Credentials Handling

- No credentials, API keys, or secrets are used
- No `.env` files or environment variable reads for secrets
- Network ports are hardcoded constants: TX 7746, RX 7225 (`src/osc/OscConstants.hpp`)

### 4) Reliability and Failure Behavior

- **Chunked image transfer**: Custom ACK protocol with per-chunk retry up to `MAX_SENDS = 5` times; marks transfer failed after max retries (`src/osc/ChunkedSend/ChunkedSend.hpp`)
- **Heartbeat / client liveness**: Receiver tracks missed heartbeats; after `MAX_MISSED_HEARTBEATS = 5` consecutive misses, reverts to broadcast mode and resets subscription state (`src/osc/OscReceiver.cpp`)
- **UDP send errors**: Logged with `WARN` and silently discarded; no retry for normal (non-chunked) messages (`src/osc/OscSender.cpp`)
- **Port binding retry**: If RX port is in use, increments port number and retries up to 20 times (`src/osc/OscReceiver.cpp`)
- **No retry** for non-chunked outbound messages (param updates, cable acks, etc.)
- **No timeout policy** for individual non-chunked messages

### 5) Observability for Integrations

- Logging around external calls: Yes — `WARN` on UDP send failure; `INFO` on successful bind; `WARN` on heartbeat timeout and client loss
- Metrics/tracing: None
- Missing visibility: No confirmation that `/set/param/value`, `/remove/cable` succeeded; ack messages are stubbed with `// + tx ack` comments in the code

### 6) Evidence

- `src/osc/OscSender.cpp` (UDP send, broadcast/direct modes)
- `src/osc/OscReceiver.cpp` (route dispatch, heartbeat, port retry)
- `src/osc/OscConstants.hpp` (hardcoded ports)
- `src/util/Network.hpp` (broadcast address calculation)
- `src/osc/ChunkedSend/ChunkedSend.hpp` (MAX_SENDS retry)
- `src/texture/Catalog.hpp` (static registry)
