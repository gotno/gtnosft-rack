# Codebase Concerns

## Core Sections (Required)

### 1) Top Risks (Prioritized)

| Severity | Concern | Evidence | Impact | Suggested action |
|----------|---------|----------|--------|------------------|
| High | No authentication — any LAN host can modify the running patch | `src/osc/OscConstants.hpp`, `src/osc/OscReceiver.cpp` | Malicious or accidental OSC messages can set params, add/remove cables, or open arbitrary patch files | Add an allowlist of client IPs or a shared-secret handshake |
| High | Detached threads in `ChunkedManager::processChunked` capture `this` with no lifetime guarantee | `src/osc/ChunkedManager.cpp:105` | Use-after-free or crash if plugin is torn down while a chunked send is in flight | Replace detached thread + action queue with a timer-based retry in the Interval/Timer pattern |
| Medium | `/add/cable` and `/remove/cable` bypass VCV Rack's undo history | `src/osc/OscReceiver.cpp:367,404` (TODO comments) | User `Ctrl+Z` after remote cable operation produces undefined/incorrect results | Use `APP->history->push(...)` with appropriate `HistoryAction` |
| Medium | `inline static` caches (`Catalog::registry`, `ModuleLightsBundler::lights`, `ModuleParamsBundler::params`) never reset on patch load | `src/texture/Catalog.hpp`, `src/osc/Bundler/ModuleLightsBundler.hpp` | Stale IDs and incorrect light state after user loads a new patch | Hook into Rack's patch load/save events to clear caches |
| Low | Missing early-exit in `ProcessMessage` when still in broadcast mode | `src/osc/OscReceiver.cpp:111` (TODO comment) | Minor: spurious route handler invocations before `/register` is received | Add `if (osctx->isBroadcasting()) return;` guard at top of `ProcessMessage` |

### 2) Technical Debt

| Debt item | Why it exists | Where | Risk if ignored | Suggested fix |
|-----------|---------------|-------|-----------------|---------------|
| Missing ack/confirmation for state-mutating commands | Stubs left as `// + tx ack` and `// + tx fail` | `src/osc/OscReceiver.cpp` (set param, add/remove cable) | Client cannot know if an operation succeeded; silent failures | Implement ack bundlers and send them after each mutation |
| Heartbeat should bypass normal send queue | OSC queue can back up; heartbeat latency inflates missed-heartbeat count | `src/osc/OscSender.cpp:56` (TODO), `src/osc/SubscriptionManager.cpp:34` (TODO) | Spurious client disconnects under load | Implement priority deque in `OscSender` |
| `ModuleStructureBundler` has a hardcoded param-type override for `Befaco/Muxlicer` | Quick fix for one known edge case | `src/osc/Bundler/ModuleStructureBundler.hpp:32` | Grows unmaintainably as more modules need overrides | Introduce a general config/override table loaded from file |
| Large commented-out code blocks in `Renderer.hpp` | Evolutionary refactoring, old approaches left in place | `src/texture/Renderer.hpp:190–229` | Confuses future contributors about active API surface | Remove dead code; retain any needed history in git |
| Enum naming inconsistency | No enforced style | `SubscriptionType::LIGHTS` (SCREAMING) vs `SendMode::Broadcast` (Pascal) | Low — cosmetic | Standardize on one style when touching these files |

### 3) Security Concerns

| Risk | OWASP category | Evidence | Current mitigation | Gap |
|------|----------------|----------|--------------------|-----|
| Unauthenticated remote command execution (set params, add cables, open patch files) | A01 Broken Access Control | `src/osc/OscReceiver.cpp` — all routes unauthenticated | None | No allowlist, no token, no auth handshake |
| Arbitrary file open via `/patch/open` | A01 Broken Access Control | `src/osc/OscReceiver.cpp:417` | None | Any LAN host can instruct Rack to load an arbitrary file path |
| No input sanitization on OSC string arguments | A03 Injection | `src/osc/OscReceiver.cpp` (`/patch/open`, `/get/module_structure`, `/add/cable` color) | oscpack type-checking only | String values passed directly to Rack API without sanitization |

### 4) Performance and Scaling Concerns

| Concern | Evidence | Current symptom | Scaling risk | Suggested improvement |
|---------|----------|-----------------|-------------|-----------------------|
| `Renderer.cpp` is the largest and highest-churn file (19.6 KB, 10 commits in 90 days) | Scan output, `src/texture/Renderer.cpp` | Complex rendering logic; multiple rendering paths (panel, overlay, knob, slider, port) | Harder to maintain as more module types require special handling | Consider splitting per-widget-type rendering into separate files |
| Per-chunk detached threads in `ChunkedManager` | `src/osc/ChunkedManager.cpp:105` | One thread per active chunked send per retry interval | Could exhaust threads with concurrent texture requests | Use `Timer::setTimeout` instead of raw detached threads |
| Subscription tick fires every 30 ms regardless of whether anything changed | `src/osc/OscConstants.hpp` (`SUBSCRIPTION_SEND_DELAY`), `src/osc/SubscriptionManager.cpp` | Low overhead when nothing changes (bundler is noop) | Grows with number of subscribed modules | Already has `inFlight` guard; acceptable for now |

### 5) Fragile/High-Churn Areas

| Area | Why fragile | Churn signal | Safe change strategy |
|------|-------------|-------------|----------------------|
| `src/texture/Renderer.cpp` | Many rendering paths; depends on Rack internals for framebuffer/NanoVG access; ongoing perf work | 10 commits in 90 days | Add integration tests; isolate per-widget rendering; read Rack changelog before updating SDK |
| `src/osc/OscReceiver.cpp` | All inbound routes; grows with every new API endpoint; thread-boundary marshalling must be correct | 10 commits in 90 days | Keep route handlers thin (parse args + `enqueueAction` only); logic belongs in bundlers or render-thread lambdas |
| `src/texture/Catalog.cpp` / `Catalog.hpp` | Static registry state; recently refactored to `unordered_map` for perf | 4 commits in 90 days | Ensure cache invalidation on patch load before adding new texture types |
| `src/osc/Bundler/ModuleLightsBundler.cpp` | Hot path; recent optimization work | 3 commits in 90 days | Benchmark before and after changes; avoid STL containers with per-frame allocations |

### 6) Resolved Design Decisions

1. **Auth scope**: Trusted LAN only — no authentication required for now. Security concerns in §3 are acknowledged but deferred.
2. **Undo history**: Ideally `/add/cable` and `/remove/cable` should integrate with Rack's undo stack, but this is deferred — not a current priority.
3. **Multi-client**: Planned eventually. The target architecture is **broadcast → register → multicast** (all clients join a single multicast group) rather than the current broadcast → unicast model. Not in scope for current work.
4. **Testing**: Desired but blocked — no established approach for testing a C++ VCV Rack plugin. [TODO] Investigate options: mocking the Rack API, a headless Rack runner, or testing bundlers/utils in isolation.
5. **Cache invalidation on patch load**: Desirable but not a current blocker for the MVP. Add when the MVP ships or if stale-ID bugs surface.

### 7) Evidence

- Scan output: `TODO / FIXME / HACK` section (12 items in production code)
- Scan output: `HIGH-CHURN FILES` section
- `src/osc/ChunkedManager.cpp:105` (detached thread)
- `src/osc/OscReceiver.cpp:367,404` (undo history TODOs)
- `src/osc/OscReceiver.cpp:417` (`/patch/open` route)
- `src/texture/Catalog.hpp` (static registry)
- `src/osc/Bundler/ModuleLightsBundler.hpp` (static lights map)
- `src/osc/OscSender.cpp:56`, `src/osc/SubscriptionManager.cpp:34` (heartbeat priority TODOs)
