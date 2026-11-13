# Plan 004: Reject invalid scale and size arguments in /get/texture

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report. Do not improvise. When done, update the status row for this plan
> in `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**:
> `git diff --stat e444a8f..HEAD -- src/osc/OscReceiver.cpp src/osc/TextureRequest.hpp tests/test_texture_request.cpp API.md`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition. Expect API.md changes from plan 001; those are
> fine as long as the `/get/texture` section (around lines 683-715) still
> matches.

## Status

- **Priority**: P2
- **Effort**: S
- **Risk**: LOW
- **Depends on**: plans/001-finish-route-rename.md (soft: both edit API.md, so land 001 first to avoid conflicts)
- **Category**: bug / security (input validation)
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

`/get/texture` checks only the texture id. The scale (float), height and width
(int32) arguments come straight from the network unvalidated:

- A NaN scale goes into `PendingTextureKey`, a `std::tuple` used as a
  `std::map` key. NaN breaks the map's strict-weak-ordering requirement,
  which is undefined behavior: lookups and coalescing can misbehave or crash.
- Zero, negative or huge values reach `Renderer::getScaleFromRecipe`, where
  they produce zero or negative scales, or enormous framebuffers. A huge size
  can exhaust memory or stall the UI thread.

Rejecting bad values up front, with a log line and no reply (matching the
existing invalid-id behavior), makes the route robust to buggy clients.

## Current state

- `src/osc/OscReceiver.cpp:531-592`: the `/get/texture` route.
  ```cpp
  routes.emplace(
    "/get/texture",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t textureId = (args++)->AsInt64();

      if (textureId <= 0) {
        INFO("/get/texture received invalid texture id %ld", textureId);
        return;
      }

      int32_t sequenceId = (args++)->AsInt32();

      // three possible argument combinations:
      // 1. float scale
      // 2. int32 height
      // 3. int32 height, int32 width
      float scale{-1.f};
      int32_t height{-1}, width{-1};

      // float (scale) or int32 (height)
      if (args->IsFloat()) {
        scale = args->AsFloat();
      } else if (args->IsInt32()) {
        height = args->AsInt32();
      } else {
        INFO(
          "/get/texture %ld scale (float) or height (int32) param was neither",
          textureId
        );
        return;
      }
      ++args;

      // optional int32 (width)
      if (args->IsInt32()) width = (args++)->AsInt32();

      // we don't have a client, ignore
      if (osctx->isBroadcasting()) return;

      PendingTextureKey key{textureId, scale, height, width};
  ```
- `src/osc/OscReceiver.hpp:91`:
  `using PendingTextureKey = std::tuple<int64_t, float, int32_t, int32_t>;`
  is the key of `std::map pendingTextures`.
- `src/osc/OscReceiver.cpp:270-276` (`renderPendingTexture`) picks the recipe:
  `scale > 0.f` → `Recipe(scale)`, else `width > 0` → `Recipe(height, width)`,
  else `Recipe(height)`.
- `src/texture/Renderer.cpp:580-600` (`getScaleFromRecipe`) computes
  `recipe.height / (framebuffer->box.size.y * pixelRatio)`, and likewise for
  width. Zero or negative inputs give zero or negative scales.
- Real clients: RackSim requests heights of 1024, 512, 256 and 128
  (`FULL_TEXTURE_HEIGHT >> level`). `tools/bench/overlay_bench.py` defaults to
  `--height 512` and accepts `--scale`.
- `API.md` documents `/get/texture` at lines ~683-715. The notes end with:
  ```
  - A non-positive or unknown id, or a render failure, produces no reply.
  ```
- Pattern for a pure, header-only helper with Rack-free tests:
  `src/osc/ParamWrite.hpp` (namespace `gtnosft::param_write`, `inline`
  functions, only std headers) and its test `tests/test_param_write.cpp`:
  ```cpp
  #include "test.hpp"
  #include "osc/ParamWrite.hpp"

  namespace pw = gtnosft::param_write;

  TEST(param_write_only_overrides_the_noise_plethora_encoder) {
    const auto* entry = pw::findOverride("Befaco", "NoisePlethora", 6);
    CHECK(entry);
    ...
  ```
  `tests/run.sh` compiles every `tests/*.cpp` automatically, with `-Isrc`.
  The framework is `TEST(name)`, `CHECK(expr)` and `CHECK_EQ(a, b)` from
  `tests/test.hpp`.
- Log style for rejected requests is `INFO("/get/texture ...", ...)` followed
  by `return;`.

**Assumed limits (confirm with the maintainer if possible; otherwise use
these):** scale must be finite and in `[0, 16]`. Height, and width when given,
must be in `[1, 8192]`. A 3U panel at scale 16 is about 6000 px tall, and
clients today ask for at most 1024.

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Rack-free tests (filter) | `./tests/run.sh texture_request` | new tests pass |
| Rack-free tests (all) | `./tests/run.sh` | all pass |
| Build plugin | `./build-wsl.sh -j8` | exit 0 (never run two build wrappers at once) |
| Address check (if plan 001 landed) | `python3 tools/check_api_addresses.py` | `OK: ...` |

## Scope

**In scope**:
- `src/osc/TextureRequest.hpp` (create)
- `tests/test_texture_request.cpp` (create)
- `src/osc/OscReceiver.cpp` (only the `/get/texture` route body)
- `API.md` (only the `/get/texture` notes list)

**Out of scope**:
- `Renderer`, `Catalog` and `Recipe`. Validation belongs at the network
  boundary.
- Changing the argument forms or the reply. Invalid requests still get no
  reply, as today.
- The `Makefile`. `src/osc/` is already covered by `SOURCES`, and the new file
  is header-only.

## Git workflow

- Current branch, or `advisor/004-validate-texture-args` if asked.
- **Do not commit.** The maintainer reviews and commits.

## Steps

### Step 1: Write the failing tests

Create `tests/test_texture_request.cpp`:
```cpp
#include "test.hpp"
#include "osc/TextureRequest.hpp"

#include <limits>

namespace tr = gtnosft::texture_request;

TEST(texture_request_accepts_positive_finite_scales_up_to_the_cap) {
  CHECK(tr::validScale(0.5f));
  CHECK(tr::validScale(1.f));
  CHECK(tr::validScale(tr::MAX_SCALE));
}

TEST(texture_request_rejects_bad_scales) {
  CHECK(!tr::validScale(0.f));
  CHECK(!tr::validScale(-1.f));
  CHECK(!tr::validScale(tr::MAX_SCALE * 2.f));
  CHECK(!tr::validScale(std::numeric_limits<float>::quiet_NaN()));
  CHECK(!tr::validScale(std::numeric_limits<float>::infinity()));
}

TEST(texture_request_accepts_dimensions_in_range) {
  CHECK(tr::validDimension(1));
  CHECK(tr::validDimension(1024));
  CHECK(tr::validDimension(tr::MAX_DIMENSION));
}

TEST(texture_request_rejects_dimensions_out_of_range) {
  CHECK(!tr::validDimension(0));
  CHECK(!tr::validDimension(-1));
  CHECK(!tr::validDimension(tr::MAX_DIMENSION + 1));
}
```

**Verify**: `./tests/run.sh texture_request` → fails to compile
(`osc/TextureRequest.hpp: No such file`). This is expected.

### Step 2: Add the helper

Create `src/osc/TextureRequest.hpp`:
```cpp
#pragma once

#include <cmath>
#include <cstdint>

// limits for /get/texture size arguments, checked before a request is queued
namespace gtnosft::texture_request {

inline constexpr float MAX_SCALE = 16.f;
inline constexpr int32_t MAX_DIMENSION = 8192;

inline bool validScale(float scale) {
  return std::isfinite(scale) && scale > 0.f && scale <= MAX_SCALE;
}

inline bool validDimension(int32_t pixels) {
  return pixels > 0 && pixels <= MAX_DIMENSION;
}

} // namespace gtnosft::texture_request
```

**Verify**: `./tests/run.sh texture_request` → the 4 new tests pass.

### Step 3: Validate in the route

In `src/osc/OscReceiver.cpp`, add `#include "TextureRequest.hpp"` next to the
other `#include "..."` lines for `src/osc/` headers. Then:

- In the float branch, right after `scale = args->AsFloat();`, add:
  ```cpp
  if (!gtnosft::texture_request::validScale(scale)) {
    INFO("/get/texture %ld invalid scale %f", textureId, scale);
    return;
  }
  ```
- In the int32 branch, right after `height = args->AsInt32();`, add:
  ```cpp
  if (!gtnosft::texture_request::validDimension(height)) {
    INFO("/get/texture %ld invalid height %d", textureId, height);
    return;
  }
  ```
- After the optional width is read
  (`if (args->IsInt32()) width = (args++)->AsInt32();`), add:
  ```cpp
  if (width != -1 && !gtnosft::texture_request::validDimension(width)) {
    INFO("/get/texture %ld invalid width %d", textureId, width);
    return;
  }
  ```
  `-1` is the "not provided" sentinel, so an explicit `-1` from a client is
  treated as "no width". That matches today's behavior.

Keep every check before `if (osctx->isBroadcasting()) return;` and before
`PendingTextureKey key{...}` is built.

**Verify**: `grep -n 'texture_request::valid' src/osc/OscReceiver.cpp` → 3
matches, all with line numbers lower than the `PendingTextureKey key{` line.

### Step 4: Document the limits in API.md

In the `/get/texture` section, replace the line
`- A non-positive or unknown id, or a render failure, produces no reply.` with:
```
- A non-positive or unknown id, a scale that is not a finite number in
  (0, 16], a height or width outside 1–8192, or a render failure, produces no
  reply.
```
Don't change anything else.

**Verify**: `grep -n "outside 1–8192" API.md` → one match. If
`tools/check_api_addresses.py` exists, it still prints `OK`.

### Step 5: Build and test everything

**Verify**: `./tests/run.sh` → all pass (existing + 4 new);
`./build-wsl.sh -j8` → exit 0.

## Test plan

- Four Rack-free tests in `tests/test_texture_request.cpp` (Step 1). They
  cover the valid range, both bounds, zero, negatives, NaN and infinity.
- The route wiring is a straight call to those helpers. It is checked by grep
  (Step 3) rather than an OSC-level test, since `OscReceiver` needs a full
  `CtrlWidget`.
- Optional manual check for the maintainer: with the bench tool,
  `overlay_bench.py run --scale 0` (or `--height 0`) yields no textures, and
  ctrl's `log.txt` shows an `invalid scale` / `invalid height` line.

## Done criteria

- [ ] `./tests/run.sh texture_request` → 4 tests pass
- [ ] `./tests/run.sh` → all pass
- [ ] `./build-wsl.sh -j8` exits 0
- [ ] `grep -c 'texture_request::valid' src/osc/OscReceiver.cpp` → `3`
- [ ] API.md `/get/texture` notes mention the limits
- [ ] `git status` shows only in-scope files changed or created
- [ ] `plans/README.md` status row updated

## STOP conditions

- The maintainer gives different limits. Use theirs, and update the constants,
  tests and API.md text consistently.
- The route's argument parsing differs from the excerpt (e.g. new argument
  forms were added).
- A known client legitimately needs sizes above the caps (search
  `tools/bench/*.py` for `--height`/`--scale` defaults). Report instead of
  raising the caps silently.

## Maintenance notes

- If the renderer gains its own size limits (e.g. GPU max texture size),
  `MAX_DIMENSION` should not exceed them.
- New `/get/texture` argument forms must validate before building
  `PendingTextureKey`. A float in the key must never be NaN.
- `OscReceiver.cpp` is large. A future split should keep this validation at
  the route boundary.
