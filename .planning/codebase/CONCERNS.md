# Codebase Concerns

**Analysis Date:** 2024-01-19

## Thread Safety Issues

**Race Condition in ChunkedSend ID Counter:**
- Issue: `idCounter` is a non-atomic static variable in `src/osc/ChunkedSend/ChunkedSend.hpp:16` used in constructor `src/osc/ChunkedSend/ChunkedSend.cpp:6`. Concurrent construction from multiple threads can produce duplicate IDs.
- Files: `src/osc/ChunkedSend/ChunkedSend.hpp`, `src/osc/ChunkedSend/ChunkedSend.cpp`
- Impact: Colliding chunk IDs cause failed acknowledgements and transmission retries. Remote client receives mixed data from multiple concurrent texture sends.
- Fix approach: Change `inline static int64_t idCounter{0};` to `inline static std::atomic<int64_t> idCounter{0};`

**Potential Race in SendMode Switching:**
- Issue: `SendMode sendMode` in `src/osc/OscSender.hpp:37` is accessed without locking by `isBroadcasting()` (line 47), `setBroadcasting()` (line 43), and `setDirect()` (line 50-52). The sender thread reads `sendMode` in `sendBundle()` (line 68) while the receiver thread or main thread can modify it via `/register` message handler.
- Files: `src/osc/OscSender.hpp`, `src/osc/OscSender.cpp`, `src/osc/OscReceiver.cpp:132`
- Impact: Intermediate state during mode switch can cause packets sent to wrong destination or using wrong socket configuration.
- Fix approach: Protect `sendMode` with a mutex. Create `setDirectThreadSafe()` method locked during endpoint change.

**Heartbeat Tracking Race:**
- Issue: `lastHeartbeatRxTime` in `src/osc/OscReceiver.hpp:62` is read in heartbeat interval callback (line 84) and written in `/keepalive` route handler (line 144). Both execute on different threads with only partial mutex coverage (only line 81 onward, missing initialization check). `missedHeartbeats` counter (line 65) is incremented on heartbeat thread without lock (line 90).
- Files: `src/osc/OscReceiver.cpp:75-103`, `src/osc/OscReceiver.cpp:139-146`
- Impact: Concurrent writes to `lastHeartbeatRxTime` and `missedHeartbeats` without consistent locking can cause incorrect timeout detection or missed keepalive handling.
- Fix approach: Lock entire heartbeat interval work, including initial initialization check. Ensure `/keepalive` handler uses same `heartbeatMutex`.

**ChunkedSend statusMutex Incomplete Coverage:**
- Issue: `statusMutex` in `src/osc/ChunkedSend/ChunkedSend.hpp:39` protects map accesses (lines 26, 36, 43) but `failed` flag (line 29) is atomic but `sendSucceeded()` at line 85 checks `chunkAckTimes.size()` under lock while `sendFailed()` at line 89 reads non-atomic `failed` without lock.
- Files: `src/osc/ChunkedSend/ChunkedSend.cpp:24-91`
- Impact: `sendFailed()` can race with `registerChunkSent()` setting failed flag, causing wrong state reports in `processChunked()`.
- Fix approach: Always lock for state checks, or make all state checks atomic. Use `failed.load()` consistently in `sendFailed()`.

**Detached Thread in ChunkedManager:**
- Issue: `src/osc/ChunkedManager.cpp:105-111` spawns a detached thread that captures `this` and `id`. If ChunkedManager is deleted before the 200ms delay completes, the captured `this` pointer becomes invalid.
- Files: `src/osc/ChunkedManager.cpp:105-111`
- Impact: Use-after-free when ChunkedManager is destroyed. Crash when timer fires 200ms later and calls `ctrl->enqueueAction()`.
- Fix approach: Store thread handles in a vector, join them in destructor. Use `std::shared_ptr<ChunkedManager>` or implement proper cleanup with `stopProcessing()` method that waits for pending threads.

**Action Queue Potential Double-Lock:**
- Issue: `processActionQueue()` in `src/OSCctrl.cpp:75-81` locks `actionMutex` and executes arbitrary lambdas. If a lambda calls `enqueueAction()`, it tries to acquire the same mutex again, causing deadlock (non-recursive mutex).
- Files: `src/OSCctrl.cpp:75-81`, `src/OSCctrl.cpp:70-73`
- Impact: Any action that internally enqueues another action will deadlock. This happens in routes like `/get/module_structure` (line 196 captures `this` and may trigger further actions).
- Fix approach: Use a recursive mutex (`std::recursive_mutex`) or drain queue to a local vector first, then execute outside the lock.

## Known Issues & Bugs

**UDP Packet Loss Handling:**
- Problem: OSC sender in `src/osc/OscSender.cpp` has no retry logic for individual message sends. If a UDP packet is dropped, no detection or resend occurs unless client acknowledges chunks. Chunks in flight depend on application-level `/ack_chunk` messages.
- Files: `src/osc/OscSender.cpp:64-88`, `src/osc/OscReceiver.cpp:149-155`
- Trigger: Network congestion or packet drops cause silent failures. Large texture sends timeout silently.
- Workaround: Clients must implement aggressive retry-all pattern when chunks don't complete within timeout.

**Broadcast Address Determination Failure Not Handled:**
- Problem: `src/osc/OscSender.cpp:19` logs warning if broadcast address calculation fails but continues with uninitialized `broadcastEndpoint`. Subsequent sends in broadcast mode fail silently.
- Files: `src/osc/OscSender.cpp:11-22`, `src/util/Network.hpp:222-257`
- Trigger: Network adapter enumeration fails, `get_network_info()` returns empty, `calculate_broadcast_address()` returns false.
- Workaround: Manual sender endpoint configuration required; broadcast mode is unavailable.

**Error in OSC Message Bundling Silently Skipped:**
- Problem: `src/osc/Bundler/Bundler.hpp:39-52` catches `osc::OutOfBufferMemoryException` and continues bundling next message. A message that's too large for the 1452-byte buffer is skipped entirely with only a WARN log.
- Files: `src/osc/Bundler/Bundler.hpp:39-52`, `src/osc/OscSender.cpp:127-134`
- Trigger: Very large single OSC message parameter list (e.g., module with 1000+ parameters).
- Impact: Module state incompletely transmitted; client never knows some parameters exist.

**Module Widget Creation Without Engine Module:**
- Problem: `src/texture/Renderer.cpp:101-104` calls `makeModuleWidget(nullptr)` for most texture types (not Switch_frame). This creates orphaned UI widgets without corresponding engine::Module, breaking parameter access in some bundlers.
- Files: `src/texture/Renderer.cpp:101-104`, `src/osc/Bundler/ModuleStructureBundler.cpp:49`
- Impact: ModuleStructureBundler attempts to access widget->getParam() on dummy widgets, potentially returning NULL or invalid ParamQuantities.

**Compression Failure Ignored:**
- Problem: `src/osc/ChunkedSend/ChunkedImage.cpp:17-21` logs warning if QOI compression fails but continues with uncompressed data. Receiver expects compressed format.
- Files: `src/osc/ChunkedSend/ChunkedImage.cpp:17-21`
- Trigger: Memory allocation failure in qoi_encode().
- Impact: Receiver fails to decompress, texture data corrupted.

**Hardcoded Magic Numbers Without Explanation:**
- Issue: Multiple magic constants scattered throughout:
  - `MAGIC_SCALE_MULTIPLIER 2.f` in `src/texture/Renderer.cpp:7` - rendering at 1x scale creates 2x pixels, unexplained
  - `MSG_BUFFER_SIZE 1452` in `src/osc/OscConstants.hpp:3` - chosen for "best case, 1232 worst case" (comment unclear)
  - `EMPTY_BUNDLE_SIZE 16` in `src/osc/OscConstants.hpp:5` - hardcoded minimum OSC bundle header size
  - `MAX_SENDS 5` in `src/osc/ChunkedSend/ChunkedSend.hpp:28` - arbitrary retry limit
  - `maxBindRetries{20}` in `src/osc/OscReceiver.hpp:41` - tries 20 ports before giving up
  - Heartbeat `HEARTBEAT_DELAY 1000` (1 second) and `SUBSCRIPTION_SEND_DELAY 30` (30ms) in `OscConstants.hpp`
- Files: `src/osc/OscConstants.hpp`, `src/texture/Renderer.cpp:7`, `src/osc/ChunkedSend/ChunkedSend.hpp:28`, `src/osc/OscReceiver.hpp:41`
- Impact: Values tuned for specific network conditions. Changing them requires understanding rationale. UDP packet loss scenarios may require different retry counts.

## Security Considerations

**Unvalidated OSC Message Arguments:**
- Risk: OSC route handlers in `src/osc/OscReceiver.cpp:126-434` extract values without bounds checking. `/get/texture` parsing (lines 205-287) accepts float or int32 arguments but no range validation.
- Files: `src/osc/OscReceiver.cpp:126-434`
- Current mitigation: None. Negative dimensions, extreme scales accepted.
- Recommendations: Validate all numeric arguments. Reject scale < 0.1 or > 10. Reject width/height > 8192. Add string length limits for plugin/module slugs.

**Path Traversal in Patch Loading:**
- Risk: `/patch/open` route handler in `src/osc/OscReceiver.cpp:412-424` accepts arbitrary file path string without validation.
- Files: `src/osc/OscReceiver.cpp:412-424`
- Current mitigation: VCV Rack's `APP->patch->load()` likely has its own checks, but plugin doesn't validate.
- Recommendations: Whitelist allowed directories. Reject paths containing `..` or absolute paths outside patch directory.

**Network Broadcast to Everyone:**
- Risk: Initial broadcast mode `src/osc/OscSender.cpp:21` sends to network broadcast address, visible to all machines on subnet.
- Files: `src/osc/OscSender.cpp`, `src/util/Network.hpp`
- Current mitigation: Switches to direct mode when client registers, but initial discovery is broadcast.
- Recommendations: Add password/auth token to heartbeat if used in untrusted networks. Document that broadcast mode exposes patch state.

**Module Instantiation via OSC:**
- Risk: `/set/param/value`, `/add/cable`, `/remove/cable` perform module operations without authentication.
- Files: `src/osc/OscReceiver.cpp:331-410`
- Current mitigation: Only in direct mode after registration, but no auth token validation.
- Recommendations: Implement simple HMAC or token-based verification of requests.

## Memory Management Issues

**Memory Leak in malloc/free Mix:**
- Issue: `src/osc/OscReceiver.cpp:130-133` and `src/osc/OscSender.cpp:71-86` use `malloc()`/`free()` for IP address string buffers instead of standard containers.
- Files: `src/osc/OscReceiver.cpp:130-133`, `src/osc/OscSender.cpp:71-86`
- Impact: If exception thrown between malloc and free, memory leaks. Not a critical issue with small fixed-size allocations, but inconsistent with C++ style.
- Fix approach: Use `std::string` or `std::array<char, IpEndpointName::ADDRESS_STRING_LENGTH + 1>`.

**Deferred Sends Not Cleaned Up on Shutdown:**
- Issue: `deferredSends` map in `src/osc/ChunkedManager.hpp:31` contains raw pointers to ChunkedSend objects. If ChunkedManager destructor is called with deferred items pending, they leak.
- Files: `src/osc/ChunkedManager.hpp:31`, `src/osc/ChunkedManager.cpp:28-34`
- Impact: On plugin unload, deferred texture sends not freed.
- Fix approach: Add cleanup in destructor: `for (auto& pair : deferredSends) delete pair.second;`

**Bundler Cleanup on Exception:**
- Issue: If `enqueueBundler()` in `src/osc/OscSender.cpp:101-106` is called and condition_variable notify causes exception, bundler pointer in queue may leak if queue is never processed.
- Files: `src/osc/OscSender.cpp:101-106`, `src/osc/OscSender.cpp:108-150`
- Impact: Rare, but bundlers with large allocations could accumulate if queue processing fails.
- Fix approach: Use `std::unique_ptr<Bundler>` in queue instead of raw pointers.

## Performance Bottlenecks

**Blocking Network Socket in Listener Thread:**
- Problem: `src/osc/OscReceiver.cpp:46-47` creates `UdpListeningReceiveSocket` running `Run()` in blocking mode. This thread blocks on `recvfrom()` indefinitely.
- Files: `src/osc/OscReceiver.cpp:44-73`
- Cause: OSCPack library's blocking receiver. Thread joins on shutdown only after `AsynchronousBreak()` signal.
- Impact: On plugin disable/reload, shutdown waits for socket to release. Slowdown if network is sluggish.
- Improvement path: Set socket timeout or use non-blocking mode with select/epoll.

**Inefficient Module Widget Lookup on Every Render:**
- Problem: `src/osc/OscReceiver.cpp:338-347` calls `APP->scene->rack->getModule(moduleId)` for every parameter set request. No caching of module pointers.
- Files: `src/osc/OscReceiver.cpp:337-346`
- Impact: Renderer also creates temporary ModuleWidgets for each texture request instead of caching surrogates.
- Improvement path: Cache module widget pointers with LRU eviction. Reuse module surrogate widget as TODO suggests in `src/OSCctrl.cpp:235`.

**Repeated Map Lookups:**
- Problem: `src/osc/ChunkedManager.cpp:40-60` performs redundant existence checks. `isProcessing()` calls `chunkedExists()`, which calls `chunkedSends.count()`. Then `findChunked()` calls `chunkedExists()` again before `.at()`.
- Files: `src/osc/ChunkedManager.cpp:40-60`
- Impact: Negligible for small maps, but pattern repeats.

**UDP Send Creates New Socket Each Time:**
- Problem: `src/osc/OscSender.cpp:64-88` creates new UdpSocket on every `sendBundle()` call. Socket is created, connected, used once, destroyed.
- Files: `src/osc/OscSender.cpp:64-88`
- Impact: Overhead from socket creation/destruction. Potentially thousands of sockets per session.
- Improvement path: Pool socket or keep persistent connection.

**200ms Polling Loop for Chunk Retries:**
- Problem: `src/osc/ChunkedManager.cpp:105-111` spawns new thread every time a chunked send is processed, sleeps 200ms, then checks status. No exponential backoff.
- Files: `src/osc/ChunkedManager.cpp:105-111`
- Impact: For slow/lossy networks, may generate hundreds of retry threads. Each thread wakes every 200ms.
- Improvement path: Implement exponential backoff. Use single retry scheduler thread instead of spawning per-chunk.

## Fragile Areas

**OSC Parser Fragility:**
- Files: `src/osc/OscReceiver.cpp:105-124`
- Why fragile: `ProcessMessage()` catches generic `osc::Exception` after extracting args. If a route handler throws before fully consuming args, iterator state is undefined and next message parsing fails silently.
- Safe modification: Validate routes are defensive about exception safety. Document expected argument counts.
- Test coverage: No visible tests for malformed message handling. Routes with optional arguments are especially fragile (e.g., `/get/texture` line 205).

**Renderer Widget Navigation:**
- Files: `src/texture/Renderer.cpp:154-211`
- Why fragile: `renderPanel()` assumes `moduleWidget->children.front()` is a panel. Some modules may have different structure. `findFramebuffer()` does deep traversal without depth limits.
- Safe modification: Add defensive null checks after each child access. Test with various module layouts.
- Test coverage: Gaps - only tested with standard VCV Fundamental modules likely.

**Subscription State Management:**
- Files: `src/osc/SubscriptionManager.cpp:20-58`
- Why fragile: Subscription cache (`ModuleLightsBundler::lights`) is static, cleared on reset. But if enqueued actions reference old subscription, cache corruption occurs. `inFlight` map cleared without waiting for pending bundlers.
- Safe modification: Make cache per-subscription, not global static. Wait for in-flight bundlers before reset.
- Test coverage: No reset stress testing.

**Heartbeat Timeout Configuration:**
- Files: `src/osc/OscReceiver.cpp:75-103`, `src/osc/OscConstants.hpp:10,12`
- Why fragile: `maxMissedHeartbeats = 5` with `HEARTBEAT_DELAY = 1000ms` means 5-second total timeout. Hardcoded without way to adjust. Networks with jitter > 1 second will false-trigger.
- Safe modification: Make timeout configurable. Implement adaptive delay based on observed latency.
- Test coverage: None visible.

## Test Coverage Gaps

**Concurrent Send Stress:**
- What's not tested: Simultaneous texture renders with duplicate sends, rapid mode switches (broadcast → direct), concurrent heartbeat checks.
- Files: `src/osc/ChunkedManager.cpp`, `src/osc/OscSender.hpp:36-53`
- Risk: Thread safety bugs only manifest under load.

**Network Failure Scenarios:**
- What's not tested: Broadcast address lookup failure (Network adapter down), socket creation failure on port collision, UDP send exceptions.
- Files: `src/util/Network.hpp`, `src/osc/OscSender.cpp:11-22`, `src/osc/OscReceiver.cpp:44-64`
- Risk: Error paths not exercised. Silent failures possible.

**Malformed OSC Messages:**
- What's not tested: Messages with mismatched arg counts, invalid texture IDs, negative dimensions, extremely large strings.
- Files: `src/osc/OscReceiver.cpp:126-434`
- Risk: Parser crashes or infinite loops.

**Module Widget Destruction During Send:**
- What's not tested: Module removed while texture render is in flight, module widget deleted while parameter update pending.
- Files: `src/texture/Renderer.cpp`, `src/osc/OscReceiver.cpp:337-346`
- Risk: Use-after-free.

## Fragility in Concurrent Bundler Processing

**Bundler Cleanup Race:**
- Issue: `src/osc/OscSender.cpp:140-148` calls `bundler->done()` which triggers `beforeDestroy` lambda, then deletes bundler. If `done()` callback enqueues another bundler, both might process before first is deleted.
- Files: `src/osc/OscSender.cpp:108-150`
- Impact: Complex callback chains can cause unexpected bundler ordering or memory pressure.

## Missing Critical Features

**No Heartbeat Initiation from Plugin:**
- Problem: Plugin can't send initial heartbeat to register itself with client. Relies on client sending `/register` first, then starts sending heartbeats. If registration fails, client has no way to discover plugin.
- Blocks: Auto-discovery in VR client. Manual IP/port entry always required.

**No Graceful Reconnection:**
- Problem: Once client disconnects, plugin cannot re-enter broadcast mode until next heartbeat timeout (5 seconds). If client suddenly reconnects, plugin sends to old direct address.
- Blocks: Seamless VR client reconnection. 5-second dead zone.

**No Bandwidth Limiting:**
- Problem: Rapid subscriptions or texture requests can saturate network. No backpressure or rate limiting.
- Blocks: Controlling CPU/network impact in resource-constrained environments.

---

*Concerns audit: 2024-01-19*
