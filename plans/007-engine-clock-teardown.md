# Plan 007: Characterize EngineClock teardown and fix the detach path if it is reachable

> **Executor instructions**: This is an **investigate-then-decide** plan.
> Steps 1-3 gather evidence and add a test. Step 4 is a decision gate. You
> STOP there and report unless the operator has already chosen a fix option.
> Run every verification command and confirm the expected result before
> moving on. When done, update the status row for this plan in
> `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**:
> `git diff --stat e444a8f..HEAD -- src/audio/EngineClock.cpp src/audio/EngineClock.hpp src/Tx.cpp src/Tx.hpp tests/rack/`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition.

## Status

- **Priority**: P3
- **Effort**: M
- **Risk**: MED (threading against Rack engine internals)
- **Depends on**: none (benefits from plan 005's Rack-linked test gate in CI and `build.sh`)
- **Category**: bug (investigate)
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

When a Tx module is the engine's master, a background thread (`EngineClock`)
drives `engine->stepBlock()` in real time. On destruction, `~EngineClock`
waits up to 1 s for the thread to exit. If it doesn't, it **detaches** the
thread. A detached thread that is still inside `Engine::stepBlock` can outlive
the engine (or even the plugin library) at shutdown, which is a
use-after-free. An earlier audit (`docs/audit.md`) said the thread
dereferences the deleted module. That's inaccurate: the module pointer is only
compared. The real question is *when the detach path is reachable* and what
the detached thread touches afterwards. The advisor's analysis (below) says:
it is unreachable in normal GUI use, but reachable when modules are deleted
under `Engine::clear()` (headless Rack, engine teardown without widgets, and
the Rack-linked test harness). This plan confirms that with a test, then lets
the maintainer pick a fix.

## Current state

### `src/audio/EngineClock.cpp` (whole teardown + loop)

```cpp
EngineClock::EngineClock(rack::engine::Module* module) : state(std::make_shared<State>()) {
  rack::Context* context = rack::contextGet();
  if (!context) return;
  thread = std::thread(run, state, context, module);
}

EngineClock::~EngineClock() {
  if (!thread.joinable()) return;
  bool exited;
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->stop = true;
    state->cv.notify_all();
    exited = state->cv.wait_for(lock, std::chrono::seconds(1),
      [&]() { return state->exited; });
  }
  if (exited) {
    thread.join();
  } else {
    // stuck on the engine lock because we're being deleted under it
    WARN("Tx: engine clock thread busy at teardown, detaching");
    thread.detach();
  }
}

void EngineClock::run(std::shared_ptr<State> state, rack::Context* context, rack::engine::Module* module) {
  ...
  std::unique_lock<std::mutex> lock(state->mutex);
  while (!state->stop) {
    rack::engine::Engine* engine = context->engine;
    if (!engine || engine->getMasterModule() != module) { ...idle 20 ms...; continue; }
    ...
    lock.unlock();
    engine->stepBlock(frames);
    BlockClock::clock::time_point deadline = pacer.next(frames, sampleRate, BlockClock::clock::now());
    lock.lock();
    state->cv.wait_until(lock, deadline, [&]() { return state->stop; });
  }
  if (clocking) setHighResolutionTimer(false);
  state->exited = true;
  state->cv.notify_all();
}
```

After `stepBlock` returns, the thread touches only `state` (kept alive by the
`shared_ptr`) and the local `pacer`. Then it sees `stop` and exits without
re-reading `context->engine`. So the only dangerous moment is *while it is
inside or blocked in `engine->stepBlock`*.

### `src/audio/EngineClock.hpp`

`State { std::mutex mutex; std::condition_variable cv; bool stop = false; bool exited = false; }`,
plus members `std::shared_ptr<State> state; std::thread thread;`.

### `src/Tx.cpp`

- Constructor ends with `engineClock = std::make_unique<tx_af::EngineClock>(this);`
- `Tx::~Tx() { engineClock.reset(); stopStreamer(); }` (lines 39-42)
- `Tx::onRemove(const RemoveEvent&) { stopStreamer(); }` (line 67)

### Rack engine facts (Rack 2 source, `src/engine/Engine.cpp`)

- `Engine::stepBlock` takes `std::lock_guard<std::mutex> stepLock(internal->blockMutex);`
  then `SharedLock<SharedMutex> lock(internal->mutex);`.
- `Engine::removeModule(m)` holds `internal->mutex` exclusively, calls
  `removeModule_NoLock(m)` (which dispatches `onRemove` and unsets the master
  if `m` is master), then **releases the lock**.
- `Engine::clear()` holds `internal->mutex` exclusively for the whole of
  `clear_NoLock()`, which does `removeModule_NoLock(module); delete module;`
  for each module. **Modules are deleted while the engine lock is held.**
- `Engine::~Engine()` calls `clear()`, asserts that it is empty, then
  `delete internal` (which frees the mutexes).
- `ModuleWidget::setModule()` (and so `~ModuleWidget`) does
  `APP->engine->removeModule(this->module); delete this->module;`. The delete
  happens **after** the lock is released.
- `Context::~Context` deletes `patch`, then `scene` (widgets, so modules are
  removed via `ModuleWidget`), then `engine`.

### Advisor's analysis (to be confirmed by Step 2)

| Teardown path | Engine lock held during `~Tx`? | Clock thread outcome |
|---|---|---|
| GUI: delete module / clear patch / quit (via `ModuleWidget`) | No | Its current `stepBlock` finishes; it sees master ≠ Tx or `stop`; joins quickly. **No detach.** |
| `Engine::clear()` / `~Engine` with modules that have no widget (headless Rack, test harness) | Yes (exclusive) | If the thread is waiting for the lock inside `stepBlock`, it can't finish → 1 s timeout → **detach**. When `clear()` releases the lock, the detached thread runs one `stepBlock` on an engine that `~Engine` may be about to `delete`. That's a use-after-free window. |

Rack's own `core/Audio.cpp` has the same structural exposure: its device
callback calls `stepBlock`, and `~Audio` closes the stream. So this is an
edge, not a crisis.

### Test harness

Rack-linked tests: `tests/rack/test_*.cpp`, run by
`./tests/run-rack.sh [filter]`, which needs `plugin.so` (`./build-wsl.sh`) and
`$RACK_DIR/libRack.so`. Fixture pattern from
`tests/rack/test_param_interaction.cpp:68-105`:
```cpp
struct Fixture {
  rack::Context context;
  ...
  Fixture() {
    rack::contextSet(&context);
    context.event = new rack::widget::EventState;
    context.engine = new rack::engine::Engine;
    ...
    context.engine->addModule(&engineModule);
  }
  ~Fixture() {
    ...
    context.engine->removeModule(&engineModule);
    rack::contextSet(nullptr);
  }
```
That file's include preamble is
`#include <engine/Engine.hpp>` / `#undef PRIVATE` / `#include <rack.hpp>`.
Use `TEST`, `CHECK` and `CHECK_EQ` from `tests/test.hpp` (`CHECK_EQ` needs
`std::to_string`-able values).

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Build plugin | `./build-wsl.sh -j8` | exit 0 (never run two build wrappers at once) |
| Rack-linked tests | `./tests/run-rack.sh engine_clock` | `... tests, 0 failed checks` |
| Rack-free tests | `./tests/run.sh` | all pass |

## Scope

**In scope (Steps 1-3)**:
- `tests/rack/test_engine_clock.cpp` (create)

**In scope only after the decision gate (Step 5), per the chosen option**:
- `src/audio/EngineClock.cpp`, `src/audio/EngineClock.hpp`, `src/Tx.cpp`

**Out of scope**:
- Rack itself (`rack-git`, the SDK).
- `AudioStreamer` / `stopStreamer` ordering.
- `BlockClock` pacing.

## Git workflow

- Current branch, or `advisor/007-engine-clock-teardown` if asked.
- **Do not commit.** The maintainer reviews and commits.

## Steps

### Step 1: Confirm the Rack facts against the Rack tree you build with

`echo $RACK_DIR` names the tree. If it is a source tree (it has `src/`), run:
```bash
grep -n "void Engine::clear()" -A4 "$RACK_DIR/src/engine/Engine.cpp"
grep -n "void ModuleWidget::setModule" -A7 "$RACK_DIR/src/app/ModuleWidget.cpp"
grep -n "void Engine::stepBlock" -A6 "$RACK_DIR/src/engine/Engine.cpp"
```
**Verify**: `clear()` takes `std::lock_guard<SharedMutex>` and calls
`clear_NoLock()`. `setModule` calls `removeModule` and then `delete`.
`stepBlock` takes `blockMutex` then a `SharedLock`. If `$RACK_DIR` is the SDK
(headers only), skip this step and say so.

### Step 2: Write a test that measures both teardown paths

Create `tests/rack/test_engine_clock.cpp` with a minimal master module that
owns an `EngineClock`, the same way `Tx` does. Include
`"audio/EngineClock.hpp"`. The class is `tx_af::EngineClock`, and it is
compiled into `plugin.so`.

```cpp
struct ClockedModule : rack::engine::Module {
  std::atomic<int> processed{0};
  std::unique_ptr<tx_af::EngineClock> clock;
  ClockedModule() { config(0, 0, 0, 0); }
  void start() { clock = std::make_unique<tx_af::EngineClock>(this); }
  void process(const ProcessArgs&) override { ++processed; }
  ~ClockedModule() override { clock.reset(); }
};
```

Helper: `waitForSteps(module)` polls up to 2 s, in 5 ms sleeps, until
`processed > 100`, and returns whether it got there.

Tests (each sets up its own `rack::Context` with
`context.engine = new rack::engine::Engine;`, calls
`rack::contextSet(&context)`, and tears down with
`rack::contextSet(nullptr)`. Do NOT `delete context.engine` manually unless
the test needs it; check how `rack::Context::~Context` handles `engine` in your
Rack tree, and avoid a double delete):

1. `engine_clock_steps_the_engine_while_master`: `auto* m = new ClockedModule;`,
   `addModule(m)`, `setMasterModule(m)`, `m->start()` → `waitForSteps(*m)` is
   true. Clean up with `removeModule(m); delete m;`.
2. `engine_clock_joins_quickly_on_widget_style_removal`: same setup, then time
   `engine->removeModule(m); delete m;`. Expect it to take **< 500 ms**,
   meaning no detach.
3. `engine_clock_teardown_under_engine_clear`: same setup, then time
   `engine->clear();` (this deletes `m` under the lock). Run it 20 times in a
   loop (new module each time). Count iterations that took ≥ 1 s (detached).
   **Don't CHECK the count.** Print it with
   `std::printf("  detached %d/20\n", detached);`. After each iteration,
   `sleep_for(50ms)` before the next, so a detached thread can drain while the
   engine is still alive.

**Verify**: `./build-wsl.sh -j8` then `./tests/run-rack.sh engine_clock` →
tests 1-2 pass (`0 failed checks`), and test 3 prints a `detached N/20` line.
Record N.

### Step 3: Interpret

- N == 0 across 3 runs: the detach path wasn't hit even under
  `Engine::clear()`. Report "not reproduced". The likely fix is Option A below.
- N > 0: the use-after-free window is real for headless and engine-clear
  teardown. Report N and recommend Option B or C.

**Verify**: run `./tests/run-rack.sh engine_clock` 3 times, and write down
each N.

### Step 4: Decision gate. STOP and report

Report N (×3), the Step 1 confirmation, and the options below. **Do not
change `src/` until the operator picks one.**

- **Option A (doc only, S)**: fix the misleading comment in `~EngineClock`
  to say when detach can happen (deleted under `Engine::clear()`; never via
  `ModuleWidget`). Keep test 3 as a printed diagnostic.
- **Option B (no-step-after-stop, S/M)**: in `run()`, re-check `state->stop`
  under `state->mutex` immediately *before* `engine->stepBlock(frames)` and
  exit if set. This narrows the window to a thread already blocked inside
  `stepBlock`. It doesn't close it.
- **Option C (stop before destruction, M)**: stop the clock thread from
  `Tx::onRemove` (dispatched by `removeModule_NoLock`, *under* the engine
  lock). Set `stop` without waiting, so the next loop iteration exits. Then
  `~EngineClock` joins with the same 1 s timeout. On its own this doesn't
  unblock a thread waiting on the engine lock, so pair it with Option B.

### Step 5 (only after the operator chooses): implement the chosen option

Implement exactly the chosen option. For B/C, also turn test 3 into a CHECK
that the detached count doesn't increase versus the Step 3 baseline.

**Verify**: `./build-wsl.sh -j8` → exit 0; `./tests/run-rack.sh` → all pass;
`./tests/run.sh` → all pass.

## Test plan

- `tests/rack/test_engine_clock.cpp`: two asserted tests (the clock steps the
  engine; widget-style removal joins in < 500 ms) and one diagnostic (the
  detach count under `Engine::clear()`).
- Manual check for the maintainer (GUI): make Tx master, delete it, undo, and
  quit Rack. `log.txt` should not contain
  `engine clock thread busy at teardown`.

## Done criteria

Investigation phase:
- [ ] `tests/rack/test_engine_clock.cpp` exists; `./tests/run-rack.sh engine_clock` → `0 failed checks`
- [ ] Report contains Step 1 confirmation, N for 3 runs, and a recommended option
- [ ] No `src/` changes (`git diff --stat -- src/` empty)
- [ ] `plans/README.md` status row set to `BLOCKED (awaiting option choice)` or `DONE` if Option A was pre-approved and applied

## STOP conditions

- Step 4 always (decision gate), unless the operator already picked an option.
- `tx_af::EngineClock` isn't linkable from the test (symbol not exported
  from `plugin.so`). Report it. Don't change visibility flags.
- The test hangs (a deadlock in `Engine::clear()`): kill it after 30 s and
  report. That is itself an important finding.
- The Rack tree's `Engine::clear` / `ModuleWidget::setModule` differ from the
  facts above.

## Maintenance notes

- Anything else that calls `stepBlock` from a plugin thread has the same
  teardown hazard. Keep such threads off the engine after `onRemove`.
- If Rack ever changes `Engine::clear()` to delete modules outside the lock,
  the detach path becomes unreachable and test 3 should read `0/20`.
