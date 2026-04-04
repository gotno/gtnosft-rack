# OSCctrl

## What This Is

OSCctrl is a VCV Rack plugin that exposes a UDP/OSC network API for remote control and real-time state streaming. It is the audio engine backend for a VR eurorack synthesizer — the VR client subscribes to module light and parameter updates, requests rendered panel textures, and sends commands to manipulate the patch. The primary real-time path is the light subscription pipeline, which must stream state changes at frame rate (~30ms intervals).

## Core Value

Real-time light state streaming must be low-latency and allocation-free in the steady state so the VR client receives smooth, accurate visual feedback every frame.

## Requirements

### Validated

- ✓ OSC/UDP API for VCV Rack remote control — existing
- ✓ Module lights subscription with real-time streaming — existing
- ✓ Module params subscription with real-time streaming — existing
- ✓ Chunked image transfer with ACK/retry protocol — existing
- ✓ Heartbeat-based broadcast ↔ direct mode switching — existing
- ✓ Module panel/overlay texture rendering (PNG via OpenGL framebuffer) — existing
- ✓ Static light state cache (`ModuleLightsBundler::lights`) across ticks — existing

### Active

- [ ] Replace `std::string hexColor` in `LightState` with raw `NVGcolor` — eliminate heap allocation per cached light
- [ ] Remove `rack::color::toHexString()` from the comparison hot path in `LightState::update()` — only convert to hex string when actually building a message
- [ ] Replace `std::map<int64_t, LightList>` with `std::unordered_map` for O(1) module lookups
- [ ] Eliminate per-tick sort + `std::set_intersection` over `rackModuleIds` — use unordered lookup instead
- [ ] Minimize `std::string` construction for message paths in `addMessage()`

### Out of Scope

- `ModuleParamsBundler` optimization — same patterns exist but deferred; focus is lights pipeline only
- Thread safety fixes identified in CONCERNS.md — separate concern, not part of this work
- New OSC commands or features — no scope expansion

## Context

The subscription fires every 30ms (`SUBSCRIPTION_SEND_DELAY = 30`) from `SubscriptionManager::tick()`. Each tick creates a new `ModuleLightsBundler`, which iterates every subscribed module's lights and calls `LightState::update()` on each. In steady state (no lights changing), `update()` still calls `rack::color::toHexString()` for every light, allocating a `std::string` just to compare — this is pure waste per tick.

The `lights` static cache persists across bundler instances. `LightState` stores `std::string hexColor` — every cached light entry carries a heap allocation. Replacing this with raw `NVGcolor` struct comparison (4 floats) eliminates the allocation in both storage and comparison paths.

The per-tick set intersection (`getModuleIds()` → sort → `std::set_intersection`) is O(n log n) work on every tick to filter subscribed module IDs. Subscriptions are a small, slow-changing set; using an unordered lookup would be faster.

## Constraints

- **C++20**: Target standard; `std::unordered_map`, structured bindings, etc. are available
- **VCV Rack API**: Must use `rack::app::LightWidget`, `rack::color::toHexString()` (for output only), `NVGcolor` as provided by rack.hpp
- **Thread safety**: All `ModuleLightsBundler` construction runs on the main Rack thread (via `enqueueAction`); the static `lights` cache is not accessed cross-thread during normal operation
- **OSC wire format**: The `/set/s/l` message sends `hexColor` as a C-string — conversion to hex must still happen when emitting, just not during comparison

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| Store `NVGcolor` instead of `std::string` in `LightState` | Eliminates heap allocation in comparison hot path; hex conversion only on send | — Pending |
| Replace `std::map` with `std::unordered_map` for lights cache | O(1) module lookup vs O(log n); module IDs are int64_t — good hash key | — Pending |
| Eliminate sort + set_intersection, use unordered set lookup | Subscriptions are small and infrequently changed; per-tick sort is wasteful | — Pending |

---
*Last updated: 2026-04-04 after initialization*
