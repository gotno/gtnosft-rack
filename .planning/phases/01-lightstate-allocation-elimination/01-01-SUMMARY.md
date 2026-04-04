---
plan: 01-01
phase: 01
status: complete
commit: d9d5f9a
duration_minutes: 5
---

## Summary

Eliminated heap allocations from `LightState` storage and comparison hot path by replacing `std::string hexColor` with `NVGcolor color`. The hex string conversion now only occurs at OSC message send time inside the `addMessage()` lambda, not during every comparison tick.

Previously, each 30ms subscription tick called `rack::color::toHexString()` for every light just to compare whether the color changed—allocating a temporary `std::string` per light even in steady state. Now, colors are compared directly via `memcmp` on the 16-byte `NVGcolor` POD struct (4 floats: r,g,b,a), which is allocation-free and fast.

## Changes

- `src/osc/Bundler/ModuleLightsBundler.hpp`:
  - Replaced `std::string hexColor` member with `NVGcolor color`
  - Updated constructor: `color(widget->color)` instead of `hexColor(rack::color::toHexString(widget->color))`
  - Updated `update()`: Uses `memcmp(&color, &widget->color, sizeof(NVGcolor)) == 0` for comparison, assigns `color = widget->color` on change

- `src/osc/Bundler/ModuleLightsBundler.cpp`:
  - Updated `addMessage()` lambda: Calls `rack::color::toHexString(state.color).c_str()` at send time instead of `state.hexColor.c_str()`

## Verification

- [x] LightState has NVGcolor color field (not std::string hexColor)
- [x] LightState::update() uses memcmp, no toHexString() call
- [x] addMessage() lambda calls rack::color::toHexString() at send time
- [x] Build passed
