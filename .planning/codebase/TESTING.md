# Testing Patterns

**Analysis Date:** 2025-01-10

## Test Framework

**Status:** No automated testing framework detected

This is a VCV Rack plugin (audio/visual synthesis), which has inherent limitations for automated testing:
- GUI components render to graphics (visual testing requires manual inspection or screenshot comparison)
- Audio processing is realtime and depends on Rack's simulation environment
- OSC networking tests require integration with external network stack
- Module widget behavior depends on Rack's event system

**Rack Limitations:**
- Rack plugins are loaded as shared libraries (.so/.dll) into Rack runtime
- Testing requires either running Rack in headless mode or spawning Rack processes
- No standard C++ test framework integrated (e.g., Google Test, Catch2 not found)
- Unit testing isolated components possible, but full plugin testing requires Rack

## Build Configuration

**Make Targets:**
```bash
# Defined in Makefile
RACK_DIR ?= ../..
CXXFLAGS += -std=c++20
```

**No Test Targets:**
- Makefile contains no `test` target
- No test build configuration present
- `make` builds plugin binary only

**Compilation:**
```bash
# Platform detection
include $(RACK_DIR)/arch.mk

# Conditional compilation for Windows/Linux/Mac
ifdef ARCH_MAC
  LDFLAGS += -framework CoreFoundation -framework SystemConfiguration
endif
```

## Manual Testing Patterns

**Integration Testing via Rack:**
- Plugin loaded into VCV Rack GUI for manual testing
- OSC receiver on RX_PORT (31416) can be tested with external OSC clients
- OSC sender broadcasts to TX_PORT via configured endpoint
- Heartbeat mechanism validates connection (MAX_MISSED_HEARTBEATS = tracked in code)

**Test Configuration:**
```cpp
// From OscConstants.hpp (implicit from usage)
static int32_t activePort{RX_PORT};    // Listening port
IpEndpointName broadcastEndpoint;      // Broadcast target
IpEndpointName directEndpoint;         // Direct connection target
```

**Network Testing:**
- Network address calculation available: `Network::calculate_broadcast_address(broadcast_ip)`
- Allows testing broadcast vs. direct unicast modes
- Bundler queue tests via manual inspection (see commented test code)

## Test-Like Patterns in Source

**Bundler Commented Test Code:**
Located in `src/osc/Bundler/Bundler.hpp` (constructor):
```cpp
// test too many messages for one packet (multiple sends)
// for (int32_t i = 0; i < 200; ++i) {
//   messages.emplace_back("/test", [i](osc::OutboundPacketStream& pstream) {
//     pstream << i;
//   });
// }

// test single message too large for packet (skip message)
// messages.emplace_back("/test", [](osc::OutboundPacketStream& pstream) {
//   for (int32_t i = 0; i < 25000; ++i) {
//     pstream << i;
//   }
// });
```

**Purpose:**
- Tests bundler behavior with many messages (fragmentation across packets)
- Tests bundler behavior with single oversized message (exceeds buffer)
- Commented out for production but shows testing intent

## Component Isolation

**Components Testable in Isolation:**

**1. Bundler Classes:**
- Base class: `src/osc/Bundler/Bundler.hpp`
- Subclasses: `ModuleStateBundler`, `ModuleParamsBundler`, `ModuleLightsBundler`, `CablesBundler`
- Can be instantiated with minimal dependencies
- Message building can be verified without network
- Could be tested with mock `osc::OutboundPacketStream`

**2. Utility Functions:**
- `src/util/Util.hpp`: `vec2cm()`, `px2cm()`, `findModel()`, `makeModuleWidget()`
- Coordinate conversion (pixels to centimeters) - simple mathematical functions
- Model lookup - requires Rack plugin context
- Could be unit-tested with stubs for Rack objects

**3. Network Utilities:**
- `src/util/Network.hpp`: `calculate_broadcast_address()`
- Platform-specific socket utilities
- Could be tested with mock socket interfaces

**4. Timer/Interval:**
- `src/util/Timer.hpp`: Complete interval/timer implementation
- Thread-based callback timer
- Could be tested with mock clock functions

## Testing Strategy Recommendations

**For Plugin Integration Testing:**
- Use Rack's headless mode if available: `./Rack -d` (development headless)
- Write external OSC client to test receiver routes
- Log OSC messages sent to verify correct bundling
- Use Rack's internal logging: existing `WARN()` macro usage

**For Component Unit Testing:**
- Extract bundler implementations into library that doesn't depend on Rack GUI
- Mock Rack's `OutboundPacketStream` for bundler tests
- Create test harness for network calculation functions
- Mock `std::thread` for interval timer testing

**Current Error Validation:**
- Try/catch used in `OscSender::OscSender()`: catches `std::exception&`
- OSC library exceptions: `catch (osc::OutOfBufferMemoryException& e)`
- Early returns on null: `if (!moduleWidget) return;`
- Rack framework warning logs: `WARN("failed to start OSC receiver after %d attempts")`

## Mocking Challenges

**Difficult to Mock:**
- `rack::app::ModuleWidget` - Deep inheritance hierarchy
- `rack::widget::Widget` - Rendering system dependencies
- `osc::OutboundPacketStream` - oscpack internals
- `std::thread` - System thread API

**Possible to Mock:**
- `std::function<void()>` callbacks - Already using in bundlers
- `Interval` timer - Pure C++ implementation, mockable with fake clock
- Network socket (with abstraction layer)
- Bundler message building (functional approach)

## Coverage Gaps

**Untested Areas:**

**1. OSC Message Parsing:**
- `OscReceiver::ProcessMessage()` - Handles incoming OSC messages
- Route dispatch system - `std::map<std::string, std::function<...>> routes`
- Message parameter extraction - error cases not covered
- Files: `src/osc/OscReceiver.cpp` (~434 lines)

**2. Rendering Pipeline:**
- `Renderer::render()` - Complex texture rendering logic
- Error handling for missing textures - returns `RenderResult` with failure status
- Framebuffer capture - graphics-dependent
- Files: `src/texture/Renderer.cpp` (~681 lines)

**3. Thread Coordination:**
- `OscSender` queue worker thread lifecycle
- `OscReceiver` listener thread startup/shutdown
- Heartbeat detection and missed heartbeat handling
- Condition variable signaling
- Files: `src/osc/OscSender.cpp`, `src/osc/OscReceiver.cpp`

**4. State Management:**
- `ModuleStructureBundler` - Complex module structure tracking
- Parameter/Light subscription state - `ModuleParamsBundler`, `ModuleLightsBundler`
- Chunked send resumption on ack - `ChunkedManager::ack()`
- Files: `src/osc/Bundler/*.cpp`

**5. Error Conditions:**
- Failed socket binding (retry logic up to 20 attempts)
- Network unreachability - broadcast address calculation failure
- Oversized OSC messages (buffer overflow handling)
- Module not found conditions - gracefully handled with NULL returns

## How Code Validates Correctness

**Logging & Debugging:**
```cpp
WARN("failed to start OSC receiver after %d attempts", maxBindRetries);
WARN("OSCctrl unable to determine network broadcast address!");
```
- Uses Rack framework logging for diagnostics
- Developers monitor console output during manual testing

**Error Result Patterns:**
```cpp
struct RenderResult {
  RenderStatus status{RenderStatus::Unknown};
  std::string statusMessage{""};
  
  bool success() { return status == RenderStatus::Success; }
  bool failure() { return status == RenderStatus::Failure; }
  bool empty() { return status == RenderStatus::Empty; }
};
```
- Status enums for operation outcomes
- Message field contains failure details
- Callers check `result.success()` before using data

**Thread-Safe Validation:**
- `enqueueAction()` pattern ensures UI updates on correct thread
- Mutex protection of action queue
- Atomic flags for worker thread shutdown signaling

**Null Pointer Validation:**
- Consistent `if (!pointer) return;` pattern
- Forward declarations ensure compile-time type checking
- Pointers initialized to NULL: `UdpListeningReceiveSocket* rxSocket = NULL;`

## Recommended Next Steps for Testing

1. **Extract testable units:** Create separate bundler test library without Rack dependency
2. **Mock external services:** Wrap Rack API calls in interface classes
3. **Use Catch2 or Google Test:** Add C++ test framework to Makefile
4. **Rack integration tests:** Script tests using Rack's automation if available
5. **Code coverage analysis:** Use gcov with test builds to identify gaps

---

*Testing analysis: 2025-01-10*
