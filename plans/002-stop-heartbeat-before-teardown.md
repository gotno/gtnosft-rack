# Plan 002: Stop the heartbeat thread before OscReceiver members are destroyed

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report. Do not improvise. When done, update the status row for this plan
> in `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**:
> `git diff --stat e444a8f..HEAD -- src/osc/OscReceiver.cpp src/osc/OscReceiver.hpp src/util/Timer.hpp`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: bug
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

`OscReceiver` runs a heartbeat thread (an `Interval`) whose callback locks
`heartbeatMutex` and uses other `OscReceiver` members. The destructor never
stops that thread explicitly. The thread is only joined when the
`heartbeatInterval` member itself is destroyed. C++ destroys members in
reverse declaration order, and `heartbeatInterval` is declared *before*
`heartbeatMutex`, `pendingTextures` and `clientSession`. So those members are
destroyed while the heartbeat thread may still be running and using them.
That is a use-after-destroy race on ctrl removal or Rack shutdown, which can
crash or hang. Stopping the interval first in the destructor removes the
race.

## Current state

- `src/util/Timer.hpp:36-56`: `Interval` owns a `std::thread worker`.
  `clear()` signals the thread via a promise and joins it. It is safe to call
  more than once: a second `set_value` throws `std::future_error`, which is
  caught, and `joinable()` is then false.
  ```cpp
  ~Interval() {
    clear();
  }
  ...
  void clear() {
    try {
      callbackPromise.set_value();
    } catch (const std::future_error& e) {
      // this enables calling clear() on intervals that are not running
    }

    if (worker.joinable()) worker.join();
  }
  ```
- `src/osc/OscReceiver.hpp` member declaration order (line numbers):
  `75 rack::Context* rackContext`, `76 lastHeartbeatRxTime`,
  `78 Interval heartbeatInterval`, `81 std::mutex heartbeatMutex`,
  `93 pendingTextures`, `94 pendingTexturesMutex`,
  `105 std::atomic<uint64_t> clientSession`.
- `src/osc/OscReceiver.cpp:52-58`: the destructor.
  ```cpp
  OscReceiver::~OscReceiver() {
    endListener();
    {
      std::scoped_lock lock(heartbeatMutex);
      ++clientSession;
    }
    gtnosft::param_interaction::clear();
  }
  ```
- `src/osc/OscReceiver.cpp:92-147`: `startHeartbeat()`. The callback runs on
  the interval's thread and uses `rackContext`, `osctx` (the `OscSender`),
  `ctrl->enqueueAction`, `heartbeatsSinceReannounce`, `heartbeatMutex`,
  `lastHeartbeatRxTime`, `missedHeartbeats`, and `dropClient(...)`, which
  touches `clientSession` and the pending-texture map.
- `src/Ctrl.cpp:129-150` (`CtrlWidget::~CtrlWidget`) deletes `oscrx` first,
  then stops the chunk worker and deletes `osctx`, `subman` and `chunkman`.
  So `osctx` and `ctrl` are still alive while `~OscReceiver` runs. Only the
  receiver's own members are at risk.

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Build plugin | `./build-wsl.sh -j8` | exit 0 (never run two build wrappers at once) |
| Rack-free tests | `./tests/run.sh` | all pass |

## Scope

**In scope**:
- `src/osc/OscReceiver.cpp` (the destructor only)

**Out of scope**:
- `src/util/Timer.hpp`: its semantics are correct. Don't change them.
- Reordering members in `OscReceiver.hpp`. Explicit `clear()` is clearer and
  doesn't depend on declaration order.
- `CtrlWidget::~CtrlWidget` teardown order.

## Git workflow

- Current branch, or `advisor/002-heartbeat-teardown` if asked.
- **Do not commit.** The maintainer reviews and commits.

## Steps

### Step 1: Stop the heartbeat first in `~OscReceiver`

Make `heartbeatInterval.clear();` the first statement of
`OscReceiver::~OscReceiver()`, before `endListener()`. Add a one-line comment
explaining why. For example:

```cpp
OscReceiver::~OscReceiver() {
  // join the heartbeat thread before the members its callback uses go away
  heartbeatInterval.clear();
  endListener();
  ...
```

Why first: the heartbeat callback can call `dropClient`, which bumps
`clientSession` and may enqueue work. Stopping it before the listener and
session teardown means nothing races those steps. `clear()` may block for up
to one heartbeat callback, including a blocking `ctrl->enqueueAction`. That
is acceptable at teardown, because the UI thread is the one destroying the
widget, and `enqueueAction` is non-blocking. Confirm this in Step 2.

**Verify**: `grep -n -A3 'OscReceiver::~OscReceiver' src/osc/OscReceiver.cpp`
shows `heartbeatInterval.clear();` as the first statement.

### Step 2: Confirm `enqueueAction` cannot block on the UI thread

Read `CtrlWidget::enqueueAction` in `src/Ctrl.cpp`. Confirm it only locks a
mutex and pushes to a queue. It must not wait for the UI thread to drain the
queue. If it waits on the UI thread, joining the heartbeat thread from the UI
thread could deadlock, so STOP.

**Verify**: `grep -n -A15 'void CtrlWidget::enqueueAction' src/Ctrl.cpp`
shows no `wait`/`condition_variable` waits.

### Step 3: Build and test

**Verify**: `./build-wsl.sh -j8` → exit 0; `./tests/run.sh` → all pass.

## Test plan

- No new automated test. The race is a teardown-timing issue that a unit test
  can't reproduce reliably, and `OscReceiver` needs a full `CtrlWidget`.
- Manual check for the maintainer (report as not done if you can't run Rack):
  in Rack, with a client connected, remove the ctrl module and undo/redo it
  ~10 times, then quit Rack. Expect no crash or hang, and no heartbeat log
  lines after the "removed" point in `log.txt`.

## Done criteria

- [ ] `heartbeatInterval.clear();` is the first statement in `OscReceiver::~OscReceiver`
- [ ] `./build-wsl.sh -j8` exits 0 and `./tests/run.sh` passes
- [ ] `git diff --stat` shows only `src/osc/OscReceiver.cpp`
- [ ] `plans/README.md` status row updated

## STOP conditions

- `CtrlWidget::enqueueAction` blocks waiting for the UI thread (Step 2).
- The destructor or member order no longer matches the excerpts.
- `Interval::clear()` is no longer idempotent (a second call would throw or
  double-join).

## Maintenance notes

- Any new background thread or `Interval` owned by `OscReceiver` needs the
  same treatment: stop it at the top of the destructor.
- Plan 003 (sendMode atomic) touches `OscSender`, which the heartbeat callback
  calls. The two plans are independent.
