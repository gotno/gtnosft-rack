# Plan 008: Remove the unused ChunkedManager parameter from SubscriptionManager and the dead ChunkedSend logging helpers

> **Executor instructions**: Follow the steps in order. Run every verification
> command and confirm the expected result before moving on. If a STOP
> condition occurs, stop and report. Don't improvise. When done, update the
> status row for this plan in `plans/README.md`, unless a reviewer dispatched
> you and told you they maintain the index.
>
> **Drift check (run first)**:
> `git diff --stat e444a8f..HEAD -- src/osc/SubscriptionManager.hpp src/osc/SubscriptionManager.cpp src/Ctrl.cpp src/osc/ChunkedSend/ChunkedSend.hpp src/osc/ChunkedSend/ChunkedSend.cpp`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition.

## Status

- **Priority**: P3
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none. If plan 006 (chunk transfer tests) is in flight, run
  this after it. Both touch `ChunkedSend`, and 006's tests make this cleanup
  safer.
- **Category**: tech-debt
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

`SubscriptionManager` takes and stores a `ChunkedManager*` it never uses. That
suggests a coupling between subscriptions and chunked transfers that doesn't
exist. `ChunkedSend` keeps two `logCompletionDuration` overloads whose only call
site is commented out. Bench instrumentation (`src/bench/`) has replaced them.
Removing both makes the ownership graph in `CtrlWidget` honest. There is no
behavior change.

## Current state

### `src/osc/SubscriptionManager.hpp` (lines 7-37)

```cpp
struct CtrlWidget;
struct OscSender;
struct ChunkedManager;

struct SubscriptionManager {
  SubscriptionManager(
    CtrlWidget* ctrl,
    OscSender* sender,
    ChunkedManager* chunkman
  );
  ~SubscriptionManager();
  ...
private:
  CtrlWidget* ctrl{NULL};
  OscSender* osctx{NULL};
  ChunkedManager* chunkman{NULL};

  std::atomic<bool> running{false};
```

### `src/osc/SubscriptionManager.cpp` (lines 1-12)

```cpp
#include "SubscriptionManager.hpp"

#include "../Ctrl.hpp"
#include "OscSender.hpp"
#include "ChunkedManager.hpp"

#include "Bundler/ModuleLightsBundler.hpp"
#include "Bundler/ModuleParamsBundler.hpp"

SubscriptionManager::SubscriptionManager(
  CtrlWidget* _ctrl, OscSender* _osctx, ChunkedManager* _chunkman
): ctrl(_ctrl), osctx(_osctx), chunkman(_chunkman) {}
```

`grep -n chunkman src/osc/SubscriptionManager.*` shows only these
declaration, constructor and member lines. There are no uses.

### `src/Ctrl.cpp` (lines 123-126), the only constructor call

```cpp
  osctx = new OscSender(this);
  chunkman = new ChunkedManager(osctx);
  subman = new SubscriptionManager(this, osctx, chunkman);
  oscrx = new OscReceiver(this, osctx, chunkman, subman);
```

`CtrlWidget::chunkman` itself stays. `OscReceiver` uses it.

### `src/osc/ChunkedSend/ChunkedSend.cpp`

Line 27, in the destructor:
```cpp
ChunkedSend::~ChunkedSend() {
  // logCompletionDuration();
  BENCH(
```
Lines 146-188: two definitions,
`void ChunkedSend::logCompletionDuration(int32_t chunkNum) { ... }` and
`void ChunkedSend::logCompletionDuration() { ... }`. Each is a short
function ending in an `INFO(...)` call. The second ends with:
```cpp
  INFO(
    "chunked send %lld total round trip %lld milliseconds",
    (long long)id,
    (long long)duration
  );
}
```
and is the last function in the file. (Confirm this with
`sed -n 140,200p src/osc/ChunkedSend/ChunkedSend.cpp` before deleting.)

### `src/osc/ChunkedSend/ChunkedSend.hpp` (lines 106-107)

```cpp
  void logCompletionDuration(int32_t chunkNum);
  void logCompletionDuration();
```
There is a blank line before them and `private:` after them.

`grep -rn logCompletionDuration src tests tools` currently matches only these
5 places (2 declarations, 2 definitions, 1 comment).

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Build | `./build-wsl.sh -j8` | exit 0, no new warnings (never run two build wrappers at once) |
| Bench build | `./build-wsl.sh -j8 BENCH=1` | exit 0 |
| Rack-free tests | `./tests/run.sh` | all pass |
| Rack-linked tests | `./tests/run-rack.sh` (after a non-BENCH build) | all pass |

## Scope

**In scope**:
- `src/osc/SubscriptionManager.hpp`, `src/osc/SubscriptionManager.cpp`
- `src/Ctrl.cpp` (one line, the `SubscriptionManager` constructor call)
- `src/osc/ChunkedSend/ChunkedSend.hpp`, `src/osc/ChunkedSend/ChunkedSend.cpp`

**Out of scope**:
- `ChunkStatus::ackedAt` (`ChunkedSend.hpp:50`). It becomes write-only after
  this change, but leave it. It is cheap and useful for future diagnostics.
- `CtrlWidget::chunkman`, `OscReceiver`'s constructor, and `ChunkedManager`.
- Anything in `src/bench/`.

## Git workflow

- Current branch, or `advisor/008-remove-dead-chunk-code` if asked.
- **Do not commit.** The maintainer reviews and commits.

## Steps

### Step 1: Drop the parameter from `SubscriptionManager`

- In `SubscriptionManager.hpp`: delete the `struct ChunkedManager;` forward
  declaration and the `chunkman` member. Change the constructor declaration to:
  ```cpp
  SubscriptionManager(CtrlWidget* ctrl, OscSender* sender);
  ```
- In `SubscriptionManager.cpp`: delete `#include "ChunkedManager.hpp"`, and
  change the constructor to:
  ```cpp
  SubscriptionManager::SubscriptionManager(CtrlWidget* _ctrl, OscSender* _osctx):
    ctrl(_ctrl), osctx(_osctx) {}
  ```
- In `src/Ctrl.cpp:125`: `subman = new SubscriptionManager(this, osctx);`

**Verify**: `grep -n "chunkman\|ChunkedManager" src/osc/SubscriptionManager.*`
→ no output.

### Step 2: Remove the dead logging helpers

- `ChunkedSend.cpp`: delete the `// logCompletionDuration();` line in the
  destructor, and delete both `logCompletionDuration` definitions plus the
  blank line before the first one.
- `ChunkedSend.hpp`: delete the two declarations, plus the blank line before
  them if that leaves two consecutive blank lines.

**Verify**: `grep -rn logCompletionDuration src tests tools` → no output.

### Step 3: Build and test

**Verify**:
- `./build-wsl.sh -j8` → exit 0
- `./tests/run.sh` → all pass
- `./tests/run-rack.sh` → all pass
- `./build-wsl.sh -j8 BENCH=1` → exit 0 (BENCH code touches ChunkedSend)
- Then rebuild without BENCH, `./build-wsl.sh -j8`, so the tree is left in
  the default configuration.

## Test plan

None new. This is pure deletion. The build proves no caller remains. Plan
006's tests, if landed, cover `ChunkedSend` behavior.

## Done criteria

- [ ] `grep -rn logCompletionDuration src tests tools` → empty
- [ ] `grep -n "chunkman\|ChunkedManager" src/osc/SubscriptionManager.*` → empty
- [ ] Both builds and both test suites pass (Step 3)
- [ ] `git diff --stat` shows only the 5 in-scope files
- [ ] `plans/README.md` status row updated

## STOP conditions

- The drift check shows `chunkman` is now used inside `SubscriptionManager`,
  or there are new `logCompletionDuration` callers. The code is no longer
  dead. Report it.
- Either build fails for a reason not obviously caused by these deletions.

## Maintenance notes

- If subscriptions ever need chunked transfers (e.g. large state snapshots),
  re-add the dependency explicitly then. Don't keep a speculative pointer.
- For per-send timing, use `src/bench/` (see `tools/bench/AGENTS.md`), not
  ad-hoc INFO logging.
