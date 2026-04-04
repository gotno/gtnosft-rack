# Roadmap: OSCctrl — ModuleLightsBundler Optimization

## Overview

This optimization eliminates allocations from the real-time light streaming hot path. Phase 1 converts `LightState` from heap-allocated hex strings to stack-allocated `NVGcolor` comparison, deferring conversion to send time. Phase 2 replaces ordered containers with unordered ones for O(1) lookups during tick processing.

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3): Planned milestone work
- Decimal phases (2.1, 2.2): Urgent insertions (marked with INSERTED)

- [ ] **Phase 1: LightState Allocation Elimination** - Replace hex string storage/comparison with raw NVGcolor in the hot path
- [ ] **Phase 2: Container & Algorithm Optimization** - Switch to unordered containers for O(1) module lookups

## Phase Details

### Phase 1: LightState Allocation Elimination
**Goal**: `LightState` comparison path is allocation-free; hex conversion only happens at send time
**Depends on**: Nothing (first phase)
**Requirements**: PERF-01, PERF-02, PERF-03
**Success Criteria** (what must be TRUE):
  1. `LightState` struct contains `NVGcolor color` field instead of `std::string hexColor`
  2. `LightState::update()` compares `NVGcolor` fields directly (no `toHexString()` call in comparison)
  3. `rack::color::toHexString()` is called only inside `addMessage()` lambda body at send time
  4. Build succeeds with no heap allocations in `LightState` storage or comparison
**Plans**: TBD

Plans:
- [ ] 01-01: TBD

### Phase 2: Container & Algorithm Optimization
**Goal**: Module lookup and subscription filtering are O(1) instead of O(log n) / O(n log n)
**Depends on**: Phase 1
**Requirements**: PERF-04, PERF-05
**Success Criteria** (what must be TRUE):
  1. `ModuleLightsBundler::lights` is typed as `std::unordered_map<int64_t, LightList>`
  2. Per-tick `rackModuleIds` filtering uses `std::unordered_set::contains()` or equivalent O(1) lookup
  3. No per-tick sort or `std::set_intersection` calls in the lights subscription path
**Plans**: TBD

Plans:
- [ ] 02-01: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 1 → 2

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. LightState Allocation Elimination | 0/? | Not started | - |
| 2. Container & Algorithm Optimization | 0/? | Not started | - |
