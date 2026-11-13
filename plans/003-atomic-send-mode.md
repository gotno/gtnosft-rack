# Plan 003: Make OscSender's send mode safe to read from any thread

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report. Do not improvise. When done, update the status row for this plan
> in `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**:
> `git diff --stat e444a8f..HEAD -- src/osc/OscSender.cpp src/osc/OscSender.hpp`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition.

## Status

- **Priority**: P2
- **Effort**: S
- **Risk**: LOW
- **Depends on**: none
- **Category**: bug
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

`OscSender::sendMode` is a plain enum. It is written under `qmutex` by
`setBroadcasting()`/`setDirect()`, but read without any lock:

- `isBroadcasting()` reads it from the heartbeat thread, the OSC receive
  thread and the UI thread.
- The sender worker reads it again *after* releasing `qmutex`, when it
  rebuilds the socket.

That is a C++ data race, which is undefined behavior. In practice, the worker
can build a socket with one mode's broadcast flag but the other mode's
endpoint, when a client connects or drops at the wrong moment. Making the
field atomic, and snapshotting it under the lock in the worker, fixes both.

## Current state

- `src/osc/OscSender.hpp`: `<atomic>` is already included (line 3).
  `enum class SendMode` is at line 24, `bool isBroadcasting();` at line 53,
  and the field at line 64:
  ```cpp
  private:
    char* msgBuffer;

    SendMode sendMode;
  ```
- `src/osc/OscSender.cpp:58-79`:
  ```cpp
  void OscSender::setBroadcasting() {
    Ctrl* module = dynamic_cast<Ctrl*>(ctrl->module);
    module->broadcasting = true;

    std::unique_lock<std::mutex> locker(qmutex);
    sendMode = SendMode::Broadcast;
    socketDirty.store(true);
  }

  bool OscSender::isBroadcasting() {
    return sendMode == SendMode::Broadcast;
  }

  void OscSender::setDirect(char* ip) {
    ...
    std::unique_lock<std::mutex> locker(qmutex);
    sendMode = SendMode::Direct;
    directEndpoint = IpEndpointName(ip, TX_PORT);
    socketDirty.store(true);
  }
  ```
- `src/osc/OscSender.cpp:287-293`, in the worker (`processQueue`):
  ```cpp
      bool dirty = socketDirty.exchange(false);
      IpEndpointName endpoint =
        (sendMode == SendMode::Broadcast) ? broadcastEndpoint : directEndpoint;

      locker.unlock();

      if (dirty || !socket) rebuildSocket(sendMode, endpoint);
  ```
  Line 293 re-reads `sendMode` after `unlock()`, so it may disagree with
  `endpoint`.
- Callers of `isBroadcasting()` (unchanged by this plan): `src/Ctrl.cpp:217,238`
  and `src/osc/OscReceiver.cpp:113,195,219,431,568,722,769,791,815`.
- Existing atomics in the same class use `std::atomic<...>` with
  `.store()`/`.load()` or `.exchange()`, e.g. `socketDirty`. Match that style.

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Build plugin | `./build-wsl.sh -j8` | exit 0 (never run two build wrappers at once) |
| Rack-free tests | `./tests/run.sh` | all pass |

## Scope

**In scope**:
- `src/osc/OscSender.hpp`
- `src/osc/OscSender.cpp`

**Out of scope**:
- The callers of `isBroadcasting()`. Their logic stays the same.
- `Ctrl::broadcasting` (the module's light flag): a separate field read by the
  audio thread for LEDs only. Leave it.
- `directEndpoint` locking. It is only read under `qmutex` already.

## Git workflow

- Current branch, or `advisor/003-atomic-send-mode` if asked.
- **Do not commit.** The maintainer reviews and commits.

## Steps

### Step 1: Make `sendMode` atomic

In `OscSender.hpp`, change `SendMode sendMode;` to
`std::atomic<SendMode> sendMode{SendMode::Broadcast};`. The constructor calls
`setBroadcasting()` anyway (`OscSender.cpp:25`), so the initializer only
documents the starting state.

**Verify**: `grep -n 'std::atomic<SendMode> sendMode' src/osc/OscSender.hpp` → one match.

### Step 2: Use explicit loads and stores

In `OscSender.cpp`:
- `setBroadcasting()`: `sendMode.store(SendMode::Broadcast);` (still under
  `qmutex`, so the mode and endpoint change together for the worker).
- `setDirect()`: `sendMode.store(SendMode::Direct);` (still under `qmutex`).
- `isBroadcasting()`: `return sendMode.load() == SendMode::Broadcast;`

**Verify**: `grep -n 'sendMode' src/osc/OscSender.cpp` shows only `.store(`
and `.load()` uses (and the Step 3 snapshot).

### Step 3: Snapshot the mode under the lock in the worker

Replace lines 287-293 with this shape:
```cpp
    bool dirty = socketDirty.exchange(false);
    SendMode mode = sendMode.load();
    IpEndpointName endpoint =
      (mode == SendMode::Broadcast) ? broadcastEndpoint : directEndpoint;

    locker.unlock();

    if (dirty || !socket) rebuildSocket(mode, endpoint);
```

**Verify**: `grep -n 'rebuildSocket(mode, endpoint)' src/osc/OscSender.cpp` →
one match, and `grep -n 'rebuildSocket(sendMode' src/osc/OscSender.cpp` → none.

### Step 4: Build and test

**Verify**: `./build-wsl.sh -j8` → exit 0 with no new warnings in
`OscSender.cpp`; `./tests/run.sh` → all pass.

## Test plan

- No new automated test. A data race is not reliably testable here, and
  `OscSender` needs a `CtrlWidget`. The change is mechanical.
- Manual check for the maintainer: connect a client, disconnect it (stop
  heartbeats until ctrl drops it), and reconnect. ctrl should resume direct
  sends (textures arrive) every time.

## Done criteria

- [ ] `sendMode` is `std::atomic<SendMode>`; every access is `.load()` / `.store()`
- [ ] The worker passes a locally snapshotted `mode` to `rebuildSocket`
- [ ] `./build-wsl.sh -j8` exits 0 and `./tests/run.sh` passes
- [ ] `git diff --stat` shows only the two in-scope files
- [ ] `plans/README.md` status row updated

## STOP conditions

- `sendMode` is read or written anywhere outside `OscSender.cpp` (check with
  `grep -rn sendMode src/`). That widens the scope.
- `OscSender` is copied or moved somewhere. `std::atomic` is not copyable, so
  the build will say so. Report it rather than adding copy constructors.

## Maintenance notes

- Any new state that the worker reads together with the endpoint should be
  snapshotted under `qmutex` in the same block, not after `unlock()`.
- `isBroadcasting()` stays a racy "hint" for callers: the mode can change
  right after it returns. That's acceptable for its current guard-style uses,
  but don't build multi-step logic on it.
