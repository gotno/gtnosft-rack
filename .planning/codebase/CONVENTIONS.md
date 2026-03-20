# Coding Conventions

**Analysis Date:** 2025-01-10

## Naming Patterns

**Files:**
- `.hpp` extension for headers
- `.cpp` extension for implementations
- Matching names: `OSCctrl.hpp` / `OSCctrl.cpp`, `OscReceiver.hpp` / `OscReceiver.cpp`
- Directory structure mirrors module types: `src/osc/`, `src/texture/`, `src/util/`

**Classes/Structs:**
- PascalCase for class and struct names: `OSCctrl`, `OscReceiver`, `ChunkedManager`, `ModuleStateBundler`
- Bundler pattern classes: `ModuleStateBundler`, `ModuleParamsBundler`, `ModuleLightsBundler`, `CablesBundler`
- Widget classes: `OSCctrlWidget`, `SceneAction`

**Methods/Functions:**
- camelCase for methods and functions: `startListener()`, `processActionQueue()`, `enqueueAction()`, `findChunked()`
- Accessor methods use pattern: `isProcessing()`, `isBroadcasting()`, `hasRemainingMessages()`
- Virtual overrides follow base class naming: `step()`, `process()`, `draw()`, `sent()`, `done()`

**Variables:**
- camelCase for local variables and members: `moduleLightSubs`, `sendMode`, `messageCursor`, `ctrlBox`
- Private members prefixed with underscore is NOT used; instead rely on access modifiers
- Member variables use initialization in class: `int32_t postSendDelayMs{0}`, `std::string name{""}` with default values

**Enums/Constants:**
- `enum class` for strongly-typed enums: `enum class TextureType`, `enum class SendMode`, `enum class RenderType`
- PascalCase for enum values: `TextureType::Panel`, `RenderStatus::Success`, `SendMode::Broadcast`
- `UPPER_CASE` for constants: `MSG_BUFFER_SIZE`, `RX_PORT`, `TX_PORT`, `MAX_MISSED_HEARTBEATS`
- Typedef patterns for function types: `typedef std::function<void(osc::OutboundPacketStream&)> messageBuilder`

**Namespaces:**
- Custom code wrapped in nested namespaces: `namespace gtnosft { namespace util { ... } }`
- Rack framework types used with `rack::` prefix or `using namespace rack;`

## Code Style

**Formatting:**
- 2-space indentation throughout: see `src/osc/OscSender.cpp`, `src/OSCctrl.cpp`
- Opening braces on same line (K&R style): `struct Bundler {`, `void step() {`, `if (!module) return;`
- Blank lines separate logical sections

**Spacing:**
- Space after control flow keywords: `if (!module)`, `while(hasRemainingMessages())`
- No space before parentheses in function calls: `startListener()`, `getNextPath()`
- Constructors use `: initialization_list` syntax on new line or same line depending on length

**Line Length:**
- Code fits within reasonable line length
- Long member initialization lists are formatted across lines

## Linting & Formatting

**Configuration:**
- No `.eslintrc` or `.prettierrc` found
- C++20 standard enforced via Makefile: `CXXFLAGS += -std=c++20`
- Platform-specific compilation handled via `arch.mk`

## Import Organization

**Header (.hpp) Order:**
1. `#pragma once` guard
2. Standard library includes: `#include <thread>`, `#include <map>`, `#include <vector>`
3. Third-party includes: `#include "rack.hpp"`, `#include "oscpack/..."`
4. Local includes: `#include "OscConstants.hpp"`, `#include "../util/Timer.hpp"`

**Implementation (.cpp) Order:**
1. Direct header: `#include "OscSender.hpp"`
2. Rack framework: `#include "rack.hpp"`
3. Relative includes in order of dependency: `#include "./dependencies"` via Makefile flags
4. Bundler/Module includes: `#include "Bundler/..."`
5. Utility includes: `#include "../util/..."`
6. Texture includes: `#include "../texture/..."`

**Path Format:**
- Relative includes from current directory: `#include "OscConstants.hpp"`
- Relative includes with subdirectory: `#include "../util/Timer.hpp"`
- Framework includes: `#include "rack.hpp"`
- Third-party library includes: `#include "oscpack/ip/UdpSocket.h"`

**Forward Declarations:**
- Used to avoid circular dependencies: `class OSCctrlWidget;`, `class OscSender;`
- Declared at top of headers before struct/class definitions

## Error Handling

**Pattern:**
- Try/catch for exceptions: `try { ... } catch(std::exception& e) { ... }` in `OscSender::OscSender()`
- Early returns for null/invalid checks: `if (!module) return;`, `if (!moduleWidget) return;`
- Rack framework logging for warnings: `WARN("failed to start OSC receiver after %d attempts", maxBindRetries);`
- OSC library exceptions caught: `catch (osc::OutOfBufferMemoryException& e) { ... }`

**Null Checks:**
- Pointer validation before use: `if (!moduleWidget)`, `if (!panel)`
- NULL initialization for pointers: `UdpListeningReceiveSocket* rxSocket = NULL;`
- Return nullptr patterns for factory functions: `findChunked()` returns null if not found

**Rack Framework Errors:**
- Uses Rack's WARN macro for logging: `WARN("message", args)`
- Uses Rack's INFO macro (implicitly, standard logging)
- Error results wrapped in structs: `struct RenderResult` with `status` field and `success()/failure()/empty()` methods

## Comments

**When to Comment:**
- Complex algorithms or non-obvious logic
- Integration points between major components
- TODOs for future work: `// TODO: do we need this with the smaller packet sizes?`
- Disabled code sections kept commented: entire function implementations commented out in `OSCctrl.cpp`

**Documentation Style:**
- Single-line comments with `//`
- Multi-line comments with `/* */` in commented code blocks
- No JSDoc/TSDoc style comments observed (C++ doesn't require them)

**Comment Types:**
- TODO comments: `// TODO: do we need this...`
- Explanatory comments above code blocks
- Inline comments after variable declarations: `int8_t maxBindRetries{20};` (no inline comments)

## Thread Safety

**Patterns Used:**

**1. Mutex + Lock Guard Pattern:**
```cpp
// In header (OSCctrl.hpp):
std::queue<Action> actionQueue;
std::mutex actionMutex;

// In implementation (OSCctrl.cpp):
void OSCctrlWidget::enqueueAction(Action action) {
  std::lock_guard<std::mutex> locker(actionMutex);
  actionQueue.push(action);
}

void OSCctrlWidget::processActionQueue() {
  std::lock_guard<std::mutex> locker(actionMutex);
  while (!actionQueue.empty()) {
    auto action = actionQueue.front();
    action();
    actionQueue.pop();
  }
}
```
- Used in `OSCctrl.hpp` for action queue (render thread safe queueing)
- Used in `OscSender.hpp` for bundler queue management

**2. Atomic Variables:**
```cpp
std::atomic<bool> queueWorkerRunning;  // In OscSender.hpp
std::map<SubscriptionType, std::atomic<bool>> inFlight;  // In SubscriptionManager.hpp
```
- Used for boolean flags that need atomic updates across threads
- Used to signal worker thread shutdown

**3. Condition Variables:**
```cpp
std::condition_variable queueLockCondition;  // In OscSender.hpp
```
- Used with mutex to wake sleeping worker threads when queue has items

**4. Thread Management:**
```cpp
std::thread listenerThread;  // OscReceiver.hpp
std::thread queueWorker;     // OscSender.hpp
```
- Listener thread created in `OscReceiver::startListener()`
- Worker thread created in `OscSender::startQueueWorker()`
- Cleanup in destructors and explicit stop methods

**5. enqueueAction() Pattern:**
- Primary mechanism for thread-safe communication between OSC worker threads and Rack's render thread
- Actions queued from other threads, processed on render thread in `step()` method
- Avoids race conditions by ensuring UI updates happen on correct thread

## Function Design

**Parameter Style:**
- Pass by const reference for large types: `const std::vector<int64_t>& moduleIds`
- Pass by value for small types: `int64_t moduleId`, `int32_t chunkNum`
- Pointers for object references: `OSCctrlWidget* _ctrl`, `OscSender* oscSender`
- Callback functions via `std::function`: `std::function<void()> callback`

**Return Values:**
- Return by value for POD types: `bool`, `int64_t`
- Return by pointer for object references: `ChunkedSend* findChunked(int64_t id)`
- Return specialized result structs: `RenderResult`, `struct RenderResult { ... }`
- Return null for "not found" patterns: `return NULL;` in `findChunked()`

**Function Size:**
- Short utility functions (1-10 lines) for simple operations
- Medium functions (20-50 lines) for core logic
- Longer implementations (100+ lines) broken across implementation files
- Largest file: `src/texture/Renderer.cpp` (681 lines) handles complex rendering logic

## Module Design

**Exports & Declarations:**
- Plugin instance declared as `extern Plugin* pluginInstance;` in `plugin.hpp`
- Models declared as `extern Model* modelOSCctrl;` in `plugin.hpp`
- Classes forward-declared in headers to avoid circular dependencies

**Header-Only Libraries:**
- `Timer.hpp`: Complete timer/interval implementation
- `Network.hpp`: Network utility functions (259 lines)
- `OscConstants.hpp`: Constant definitions

**Separation of Concerns:**
- `Bundler.hpp/cpp`: Base bundler pattern with message queueing
- `ModuleStateBundler.hpp/cpp`: Specific bundler for module state
- `OscSender.hpp/cpp`: Message transmission and queue management
- `OscReceiver.hpp/cpp`: Message reception and routing
- `SubscriptionManager.hpp/cpp`: Subscription state tracking
- `ChunkedManager.hpp/cpp`: Chunked message handling

**Struct vs Class:**
- `struct` used for most types (implicit public inheritance, public members by default)
- `class` used for types that inherit from Rack framework classes: `class OscSender`, `class OscReceiver`
- Rack base classes force use of `class` keyword

## Bundler Pattern

**Virtual Methods:**
- `virtual bool isNoop()` - Check if bundler has no messages to send (optimization to skip transmission)
- `virtual void sent()` - Called after messages are successfully bundled
- `virtual void done()` - Called after cleanup, always invoked even if noop

**Callback Pattern:**
```cpp
std::function<bool()> noopCheck = [this]() { return messages.empty(); };
virtual bool isNoop() { return noopCheck(); }

std::function<void()> onBundleSent = []() {};
virtual void sent() { onBundleSent(); }

std::function<void()> beforeDestroy = []() {};
virtual void done() { beforeDestroy(); }
```
- Allows subclasses to override behavior via lambdas
- Default implementations can be customized

**Message Building:**
- Messages stored as pairs: `pair<path_string, messageBuilder_function>`
- Builder functions: `typedef std::function<void(osc::OutboundPacketStream&)> messageBuilder`
- Message cursor tracks position: `size_t messageCursor{0}`

## Type System

**Strongly Typed:**
- `enum class TextureType` - Prevents accidental int comparisons
- `enum class SendMode` - Type-safe send mode selection
- Modern C++20 features available: `#include <std::...>`

**Template Usage:**
- `std::vector`, `std::map`, `std::queue` for containers
- `std::unique_ptr` for ownership: `std::map<int64_t, std::unique_ptr<ChunkedSend>> chunkedSends`
- `std::function` for callable types: `std::function<void()>`, `std::function<void(osc::OutboundPacketStream&)>`

## Memory Management

**Pattern:**
- Explicit `new` / `delete` used in this codebase (not modern C++ smart pointers throughout)
- `std::unique_ptr` used selectively: `std::unique_ptr<ChunkedSend>` in ChunkedManager
- Destructors handle cleanup: `delete oscrx;`, `delete chunkman;`

**Ownership:**
- Widget/Module classes manage child objects' lifetime
- Parent objects (OSCctrlWidget) delete children in destructor
- Rack framework handles Module/Widget lifecycle

---

*Convention analysis: 2025-01-10*
