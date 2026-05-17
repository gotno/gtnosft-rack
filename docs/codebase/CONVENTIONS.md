# Coding Conventions

## Core Sections (Required)

### 1) Naming Rules

| Item | Rule | Example | Evidence |
|------|------|---------|----------|
| Files | PascalCase, no separators | `OscReceiver.cpp`, `ModuleLightsBundler.hpp` | `src/osc/`, `src/osc/Bundler/` |
| Structs / classes | PascalCase | `OscSender`, `ChunkedManager`, `LightState` | `src/osc/OscSender.hpp` |
| Methods / functions | camelCase | `enqueueBundler`, `setBroadcasting`, `processQueue` | `src/osc/OscSender.hpp` |
| Member variables | camelCase, no prefix/suffix | `sendMode`, `queueWorker`, `missedHeartbeats` | `src/osc/OscSender.hpp` |
| Constructor parameters | leading underscore to disambiguate from member | `_ctrl`, `_delay`, `_callback` | `src/util/Timer.hpp`, `src/osc/OscReceiver.hpp` |
| Constants / `#define` macros | SCREAMING_SNAKE_CASE | `MSG_BUFFER_SIZE`, `TX_PORT`, `RX_PORT` | `src/osc/OscConstants.hpp` |
| Namespaces | lowercase, dotted-style (`gtnosft::util`) | `gtnosft::util::makeRackId()` | `src/util/Util.hpp` |
| Enums | PascalCase name, PascalCase or SCREAMING values (mixed in codebase) | `SendMode::Broadcast`, `RenderStatus::Success`, `LIGHTS` | `src/osc/OscSender.hpp`, `src/osc/SubscriptionManager.hpp` |

### 2) Formatting and Linting

- **Formatter**: None configured (no `.clang-format`, `.editorconfig`, or equivalent found)
- **Linter**: None configured
- **Observed style** (from source files):
  - 2-space indentation
  - Brace on same line for functions/control flow
  - `#pragma once` for header guards
  - `rack.hpp` included at top of `.cpp` files that use Rack APIs
- Run commands: N/A — no automated formatting or linting

### 3) Import and Module Conventions

- **Include order** (observed): Rack SDK first (`#include "rack.hpp"`), then stdlib headers, then local headers
- **Local includes**: relative paths from the including file (e.g., `#include "../OSCctrl.hpp"` from within `src/osc/`)
- **Vendor includes**: via `-I./dependencies` flag; included as `#include "oscpack/ip/UdpSocket.h"`
- **Header guards**: `#pragma once` universally
- No barrel/re-export headers; no path aliases

### 4) Error and Logging Conventions

- **Logging**: VCV Rack's macros — `INFO(fmt, ...)`, `WARN(fmt, ...)`, `DEBUG(fmt, ...)`. No external logging library.
- **Error strategy**:
  - OSC message parsing errors: caught at `OscReceiver::ProcessMessage` boundary with `WARN` + message discard
  - Rack API lookup failures (module not found, param not found): early `return` with optional future `// + tx fail` comment (not yet implemented)
  - Interval callback exceptions: caught in `Timer.hpp` with `WARN` + interval stop
  - UDP send errors: caught in `OscSender::sendBundle` with `WARN`
  - oscpack exceptions: `osc::Exception` (base), `osc::ExcessArgumentException`, `osc::WrongArgumentTypeException` — caught at message dispatch boundary
- **Sensitive-data redaction**: Not applicable (no credentials or PII in OSC messages)
- Many `WARN`/`INFO` calls that were added for debugging are commented out with `// DEBUG(...)` or `// INFO(...)`

### 5) Testing Conventions

- No test files exist in this repository
- No test framework is configured
- No coverage tooling
- [TODO] No documented policy for testing strategy

### 6) Evidence

- `src/osc/OscConstants.hpp` (constants naming)
- `src/osc/OscSender.hpp`, `src/osc/OscSender.cpp` (method/member naming)
- `src/osc/OscReceiver.cpp` (error handling at OSC boundary)
- `src/util/Timer.hpp` (constructor param convention, exception handling)
- `src/texture/Renderer.hpp` (struct naming, enum naming)
