# Codebase Concerns

## Core Sections (Required)

### 1) Top Risks (Prioritized)

| Severity | Concern | Evidence | Impact | Suggested action |
|----------|---------|----------|--------|------------------|
| High | No authentication — any LAN host can modify the running patch | `src/osc/OscConstants.hpp`, `src/osc/OscReceiver.cpp` | Malicious or accidental OSC messages can set params, add/remove cables, or open arbitrary patch files | Add an allowlist of client IPs or a shared-secret handshake |
| ~~High~~ Resolved | ~~Detached threads in `ChunkedManager::processChunked` capture `this` with no lifetime guarantee~~ | `src/osc/ChunkedManager.cpp` | Fixed: retries are polled from `ChunkedManager::tick()` on the UI thread; no threads | See `docs/superpowers/specs/2026-10-06-chunked-retry-tick-design.md` |
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
| ~~Per-chunk detached threads in `ChunkedManager`~~ | `src/osc/ChunkedManager.cpp` | Resolved: no threads; retries polled per UI frame | — | Retry precision now tied to UI frame rate |
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
- `src/osc/ChunkedManager.cpp` (detached thread — resolved)
- `src/osc/OscReceiver.cpp:367,404` (undo history TODOs)
- `src/osc/OscReceiver.cpp:417` (`/patch/open` route)
- `src/texture/Catalog.hpp` (static registry)
- `src/osc/Bundler/ModuleLightsBundler.hpp` (static lights map)
- `src/osc/OscSender.cpp:56`, `src/osc/SubscriptionManager.cpp:34` (heartbeat priority TODOs)

a later check:
   🔴 Critical: easy to trigger, or will crash sooner or later                 ┃
                                                                               ┃
   1. One bad UDP packet crashes Rack. oscpack checks each packet before your  ┃
      code sees it ( OscPacketListener.h:66-74 ). If the packet is malformed,  ┃
      it throws an error ( OscReceivedElements.cpp:598-604, 754-780 ), and the ┃
       try  in  ProcessMessage  doesn't catch it. The listener thread then     ┃
      dies and takes Rack with it. Sending 1 byte to port 7225 is enough.      ┃
      ✅ Fixed in c5f30a7 (ProcessPacket override catches malformed packets).   ┃
   2. A second OSCctrl in the patch crashes Rack. The constructor gives up     ┃
      early and leaves  subman  as NULL, but  step()  still calls              ┃
       subman->tick()  ( OSCctrl.cpp:102-123, 136 ).                           ┃
      ✅ Fixed in d530318 (step() skips a missing subman).                      ┃
   3. Cached widget pointers outlive their widgets. The shared light and param ┃
      caches ( ModuleLightsBundler::lights ,  ModuleParamsBundler::params )    ┃
      keep pointers to widgets. They are never cleared when OSCctrl is         ┃
      destroyed. Reverting, reloading, or undoing a module delete keeps the    ┃
      same module IDs, so the next subscribe or  /get/params_state  reads      ┃
      widgets that no longer exist.                                            ┃
      ✅ Fixed in 210623a and bcf9088 (caches cleared on invalidation and      ┃
      teardown).                                                               ┃
   4. Wrong shutdown order.  ~OSCctrlWidget  deletes  chunkman  before         ┃
      stopping the  osctx  send thread ( OSCctrl.cpp:125-130 ). Image chunks   ┃
      still waiting to be sent then call into the deleted  ChunkedManager  and ┃
      read freed image data ( ChunkedManager.cpp:70-80 ,                       ┃
       ChunkedSendBundler.cpp:28-32 ).                                         ┃
      ✅ Fixed in bcf9088 (teardown order); queued chunk bundlers now also own  ┃
      their send via shared_ptr.                                               ┃
   5. Detached retry threads. Each one wakes after 200 ms and calls            ┃
       ctrl->enqueueAction  ( ChunkedManager.cpp:85-91 ). If the module is     ┃
      removed, a patch loads, or Rack quits while a texture transfer is        ┃
      running, that call uses a freed widget.                                  ┃
      ✅ Fixed: retries run from ChunkedManager::tick() on the UI thread; no    ┃
      threads.                                                                 ┃
                                                                               ┃
   🟠 High                                                                     ┃
                                                                               ┃
   6.  chunkedSends  is shared by three threads with no lock.  /ack_chunk      ┃
      runs on the listener thread, the send-skip and after-send callbacks      ┃
      ( noopCheck ,  onBundleSent ) run on the send thread, and add/erase runs ┃
      on the UI thread. Between  chunkedExists()  and  getChunked()  another   ┃
      thread can erase the entry, which fails the assert or throws from        ┃
       .at() .  ChunkedSend::acked()  also reads its map without the lock.     ┃
      ✅ Fixed: the map is mutex-guarded, bundler callbacks hold a shared_ptr  ┃
      to their send instead of looking it up, and  acked()  takes the lock.    ┃
   7.  /patch/open  with a bad path or broken patch file crashes Rack.         ┃
       patch->load  throws ( rack-git/src/patch.cpp:313, 369, 375 ), and       ┃
      nothing catches it inside  SceneAction::step .                           ┃
   8. Client-supplied texture sizes aren't validated. A scale ≤ 0, a height ≤  ┃
      0, a size too big for the GPU, or a zero-size widget means no            ┃
      framebuffer gets created. Then  nvgImageSize  leaves  width / height     ┃
      uninitialized ( Renderer.cpp:426-533 ), leading to garbage-sized         ┃
      allocations,  glReadPixels  and a stack array in  flipBitmap .           ┃
       hashBitmap  also always reads 256 bytes, whatever size the image        ┃
      actually is ( Catalog.cpp:35 ).                                          ┃
   9. IDs are squeezed into 8 bits.  Breadcrumbs::componentId  and  frameIdx   ┃
      are  uint8_t , so IDs above 255 wrap.  getParam  can then return NULL or ┃
      the wrong widget type, and  findFramebuffer(NULL)  or                    ┃
       sliderWidget->background  dereference NULL ( Renderer.cpp:297-300 ).    ┃
      This fires during  /get/module_structure .                               ┃
   10. The overlay renderer builds a second widget on the live module          ┃
       ( Renderer.cpp:226-242 ). Some plugins' widgets store a pointer to      ┃
       themselves in their module. Once the second widget is deleted, that     ┃
       pointer is left dangling.                                               ┃
                                                                               ┃
   🟡 Medium                                                                   ┃
                                                                               ┃
   11. The heartbeat thread clears the light subscriptions and                 ┃
        ModuleParamsBundler::params  ( OscReceiver.cpp:100-103 ) while the UI  ┃
       thread may be using them.                                               ┃
   12. In  OscReceiver ,  heartbeatMutex  is destroyed before the heartbeat    ┃
       thread is joined ( OscReceiver.hpp:64-66 ).                             ┃
   13.  makeConnectedModuleWidget  adds throwaway modules to the live engine   ┃
       ( Util.cpp:39-46 ). The audio thread runs them, and if a module's       ┃
       constructor throws on the UI thread, nothing catches it.                ┃
   14.  CablesBundler  assumes every engine cable has a widget                 ┃
       ( CablesBundler.cpp:20-26 ).                                            ┃
   15. On Linux,  ifa_netmask  can be NULL and is dereferenced without a check ┃
       ( Network.hpp:170 ).                                                    ┃
                                                                               ┃
   ⚪ Low                                                                      ┃
                                                                               ┃
   16.  ParamAckBundler  captures  value  by reference, so the ack reads a     ┃
       stale stack variable ( ParamAckBundler.cpp:9 ).                         ┃
   17.  processQueue  returns on a null bundler, and  drainMailboxes  can race ┃
       into that, permanently stopping all sends.  queueWorkerRunning  is set  ┃
       inside the thread, so stopping before it starts hangs the join.         ┃
   18.  processActionQueue  holds the lock while running actions, so any       ┃
       action that enqueues another deadlocks.                                 ┃
   19. Smaller gaps:  getScaleFromRecipe  can return without a value, child    ┃
       lists are assumed to be non-empty, and  getParamQuantity()  results     ┃
       aren't null-checked.                                                    ┃
