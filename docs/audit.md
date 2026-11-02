# Architecture Audit — gotno/gtnosft-rack — 2026-11-02

**Verdict: FIX** — two confirmed thread/lifetime hazards and missing CI test
coverage should be addressed.

**Scope:** Repository-owned C++ source, build scripts and configuration,
manifest, contributor/API docs, tests, and CI. Generated binaries and
third-party dependency internals were not audited.

## Architecture at a glance

```text
plugin.cpp -> Ctrl / Tx
CtrlWidget -> OscReceiver -> UI action queue -> Catalog/Renderer
            -> ChunkedManager -> OSC bundlers -> OscSender -> UDP
CtrlWidget -> SubscriptionManager; OSC receive/send paths -> chunk/subscription managers
Tx::process -> SPSC ring -> AudioStreamer -> encoder/packet -> UdpAudioSocket
Tx -> EngineClock -> Rack engine
BENCH=1 -> instrumentation hooks across production
Rack-free tests -> packet/encoder/clock/pacer
```

## Recommendations

### Structural

- **[Track A | High | High confidence]** `src/audio/EngineClock.cpp:14-32,38-72`;
  `src/Tx.cpp:39-42` — the clock worker can detach after a one-second timeout
  while still executing engine work through a raw module pointer; `Tx` then
  destroys the clock. This permits the worker to outlive `Tx` and dereference
  it. Replace detach-on-timeout with a shutdown/lifetime protocol that cannot
  destroy the module until the worker exits.
- **[Track A | High | High confidence]** `src/osc/OscSender.cpp:71-78,260-267`;
  caller `src/osc/OscReceiver.cpp:381-386` — `setDirect()` writes `sendMode`
  under `qmutex`, but the sender worker reads it again after unlocking when
  rebuilding the socket; `isBroadcasting()` also reads mode unsynchronized
  (`OscSender.cpp:67-69`). This is a confirmed data race and can pair a stale
  endpoint with the wrong mode. Snapshot mode and endpoint together under the
  lock and synchronize status reads.
- **[Track A | Medium | High confidence]** `src/osc/OscReceiver.hpp:25-105`;
  `src/osc/OscReceiver.cpp:176-248,251-315,576-588` — `OscReceiver` combines
  packet routing, socket/thread lifecycle, heartbeat state, UI scheduling,
  texture coordination, subscriptions, and benchmark controls, with direct
  dependencies on `CtrlWidget`, managers, `Catalog`, and `Renderer`. Retain one
  transport entry point, but move route groups behind focused handlers with
  narrower interfaces and seams; this should improve locality without moving
  necessary UI-thread marshalling out of the flow.

### Quick wins

- **[Track E | Medium | High confidence]**
  `.github/workflows/build-plugin.yml:32-57,62-96` — CI builds platform
  artifacts but does not run `tests/run.sh` or the documented Python
  audio-analysis suite. Add both to CI so packet/audio regressions fail on the
  same path that validates builds. Locally, the C++ suite passed (22 tests),
  its `resyncs` filter passed, and the Python suite passed (15 tests).
- **[Tracks A/D | Medium | High confidence]** `src/plugin.cpp:8-9`,
  `src/plugin.hpp:8-9`, `plugin.json:15-27`, `AGENTS.md:44-47` — adding a
  module requires matching manual edits to declarations, registration, and
  manifest; no test references those entries (`rg` in `tests/` found none).
  Add a drift guard for model registration versus manifest slugs.
- **[Track D | Medium | High confidence]**
  `.github/workflows/build-plugin.yml:37,79-89` — the Linux/Windows toolchain
  image has no version tag or digest, while the macOS Rack SDK is explicitly
  set to 2.6.4 (`.github/workflows/build-plugin.yml:5,82`). Pin the
  image/toolchain version to make CI builds reproducible.
- **[Track C | Low | High confidence]**
  `src/osc/SubscriptionManager.hpp:20-24`;
  `src/osc/SubscriptionManager.cpp:9-12` — `chunkman` is stored and passed in
  but unused. Remove the dependency until subscriptions need it.

### Needs verification

- **[Tracks A/E | Medium | Medium confidence]**
  `src/audio/AudioStreamer.cpp:59-67`; `src/Tx.cpp:97-100` — reset calls
  `ring.clear()` after changing the streaming flag, while the producer may
  still be in `full()`/`push()`. The Rack `dsp::RingBuffer` implementation is
  external and absent here, so whether this is unsafe is unconfirmed. Verify
  its `clear()` concurrency contract; if it requires producer quiescence, add
  a shutdown handshake and focused test. This does not drive the verdict.

### Leave as-is

- Chunked texture retries, acknowledgements, back-pressure, coalescing, and
  supersession encode measured network behavior; `API.md:633-675` and
  `tools/bench/PERFLOG.md:51-69` document the protocol and measured tradeoffs.
  Avoid simplifying these without preserving their behavior.

## Documentation and verification

The root build/test instructions and the API's representative `/get/texture`
flow match the scripts and code inspected. The Rack-free C++ suite and Python
audio-analysis suite passed. The Rack plugin build was not run: the configured
default `RACK_DIR` lacks `arch.mk` and `plugin.mk` in this environment.

**Not assessed:** Rack SDK-dependent build, live Rack/UI behavior, benchmark
mock-server smoke test, third-party dependency internals, and host-side
required-check configuration.
