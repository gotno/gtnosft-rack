# Requirements: OSCctrl — ModuleLightsBundler Optimization

**Defined:** 2026-04-04
**Core Value:** Real-time light state streaming must be low-latency and allocation-free in the steady state so the VR client receives smooth, accurate visual feedback every frame.

## v1 Requirements

### Performance — Lights Pipeline

- [ ] **PERF-01**: `LightState` stores `NVGcolor` instead of `std::string hexColor`, eliminating heap allocation per cached light entry
- [ ] **PERF-02**: `LightState::update()` compares raw `NVGcolor` values directly — `rack::color::toHexString()` is not called in the hot path
- [ ] **PERF-03**: `addMessage()` lambda defers hex string conversion to send time only (inside the lambda body, not captured from `LightState`)
- [ ] **PERF-04**: `ModuleLightsBundler::lights` cache uses `std::unordered_map<int64_t, LightList>` instead of `std::map` for O(1) module lookups
- [ ] **PERF-05**: Per-tick `rackModuleIds` sort + `std::set_intersection` replaced with O(1) unordered set membership test for subscribed module filtering

## v2 Requirements

### Performance — Params Pipeline

- **PARAMS-01**: `ParamState` similarly avoids unnecessary allocations in hot path (same patterns as PERF-01/02)
- **PARAMS-02**: `ModuleParamsBundler::params` cache uses `std::unordered_map`

## Out of Scope

| Feature | Reason |
|---------|--------|
| Thread safety fixes (CONCERNS.md) | Separate concern; not part of this optimization work |
| New OSC commands or subscriptions | No scope expansion for this milestone |
| ModuleParamsBundler optimization | Deferred to v2 — same patterns, not the primary bottleneck |
| Benchmarking harness | No test infrastructure for micro-benchmarks; verify via code review |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| PERF-01 | Phase 1 | Pending |
| PERF-02 | Phase 1 | Pending |
| PERF-03 | Phase 1 | Pending |
| PERF-04 | Phase 2 | Pending |
| PERF-05 | Phase 2 | Pending |

**Coverage:**
- v1 requirements: 5 total
- Mapped to phases: 5
- Unmapped: 0 ✓

---
*Requirements defined: 2026-04-04*
*Last updated: 2026-04-04 after initial definition*
