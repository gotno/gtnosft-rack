# Plan 006: Add characterization tests for RttEstimator and ChunkedSend

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report. Do not improvise. When done, update the status row for this plan
> in `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**:
> `git diff --stat e444a8f..HEAD -- src/osc/RttEstimator.hpp src/osc/ChunkedSend/ src/osc/Bundler/ChunkedSendBundler.hpp src/osc/Bundler/ChunkedSendBundler.cpp tests/`
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition.

## Status

- **Priority**: P2
- **Effort**: M
- **Risk**: LOW (tests only; no production code changes)
- **Depends on**: none
- **Category**: tests
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

Texture delivery is a hand-rolled reliable-UDP protocol. `ChunkedSend` tracks
the state of each chunk (pending, queued, sent, acked), decides resends and
gives up. `RttEstimator` computes the adaptive retransmission timeout
(RFC 6298 with Karn's algorithm). Neither has a single test, yet both were
tuned through many benchmark iterations (see `tools/bench/PERFLOG.md`). A
subtle change, such as counting a resent chunk's RTT or breaking the
"ack while queued" case, would silently degrade textures over Wi-Fi.
Characterization tests pin today's behavior. They should land before any
refactor of the OSC/chunk code.

## Current state

### `src/osc/RttEstimator.hpp` (header-only, std-only; Rack-free test OK)

```cpp
class RttEstimator {
public:
  using duration = std::chrono::nanoseconds;
  static constexpr std::chrono::milliseconds MIN_RTO{10};
  static constexpr std::chrono::milliseconds MAX_RTO{200};

  void addSample(duration rtt) {
    double sample = (double)rtt.count();
    ...
    if (!hasSample) {
      srttNs = sample;
      rttvarNs = sample / 2.0;
      hasSample = true;
    } else {
      rttvarNs = 0.75 * rttvarNs + 0.25 * std::fabs(srttNs - sample);
      srttNs = 0.875 * srttNs + 0.125 * sample;
    }
    backoffs = 0;
  }
  void backoff() { ... if (backoffs < MAX_BACKOFFS) ++backoffs; }   // MAX_BACKOFFS = 5 (private)
  duration rto(duration granularity) {
    if (!hasSample) return MAX_RTO;
    double base = srttNs + std::max((double)granularity.count(), 4.0 * rttvarNs);
    return std::clamp<duration>(
      duration((int64_t)(base * (double)(1 << backoffs))), MIN_RTO, MAX_RTO);
  }
  duration srtt();   // zero until the first sample
  void reset();      // back to no samples, no backoffs
};
```

### `src/osc/ChunkedSend/ChunkedSend.{hpp,cpp}` (includes `rack.hpp` and oscpack; needs the Rack-linked runner)

- Abstract: `virtual ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) = 0;`
- Constructor `ChunkedSend(uint8_t* data, int64_t size)` takes ownership of
  `data`, which the destructor frees with `delete[]`.
- `init()` calls `getBundlerForChunk(0)`, sets
  `chunkSize = bundler->getAvailableBundleSpace()` and
  `numChunks = ceil(size / chunkSize)`, deletes the bundler, and resets all
  chunk states to `Pending`.
- `bool ack(int32_t chunkNum, std::chrono::nanoseconds* rtt = nullptr)`:
  returns false for an invalid or already-acked chunk. If the chunk is `Sent`
  with `sendCount == 1`, its round trip counts toward `maxRtt` (Karn). On the
  ack that completes the send, `*rtt = maxRtt` if `maxRtt > 0`.
- `bool acked(int32_t chunkNum)`.
- `int32_t takeChunksDue(std::vector<int32_t>& chunkNums, bool adaptive, nanoseconds rto, nanoseconds maxRto)`:
  `Pending` chunks are always taken. `Sent` chunks are taken once timed out:
  - adaptive: timeout = `min(rto << min(sendCount-1, 5), maxRto)`. The send
    fails if a due chunk was first sent ≥ `GIVE_UP_AFTER` (1.2 s) ago.
  - fixed: timeout = `RETRY_TIMEOUT` (200 ms). The send fails if
    `sendCount > MAX_RETRIES` (5).

  Taken chunks become `Queued`. `Queued` and `Acked` chunks are skipped. The
  return value is the number of adaptive first-send timeouts.
- `registerChunkSent(n)`: increments `sendCount` and stamps the times. The
  state becomes `Sent` *unless already `Acked`*.
- `registerChunkDropped(n)`: `Queued` → `Pending`.
- `sendSucceeded()` (all acked), `sendFailed()` (the `failed` flag).

`ChunkedSend.hpp` has a `BENCH(bench::TracePtr trace;)` member. Its layout
differs between `BENCH=0` and `BENCH=1` builds. **The plugin must be built
without `BENCH=1`** for these tests, because the test TU compiles with BENCH
off.

### `src/osc/Bundler/ChunkedSendBundler.hpp` (constructor used by the test subclass)

```cpp
struct ChunkedSendBundler : virtual Bundler {
  ChunkedSendBundler(
    std::string address, int64_t chunkedSendId, int32_t sequenceId,
    int32_t chunkNum, int32_t numChunks, int32_t chunkSize,
    int64_t totalSize, uint8_t* data, int32_t thisChunkSize);
  size_t getAvailableBundleSpace();
  ...
};
```
`MSG_BUFFER_SIZE` is 1232 (`src/osc/OscConstants.hpp:4`), so `chunkSize` is a
bit over 1100 bytes. Tests must not hard-code it; read `chunkSize` after
`init()`.

### Test harnesses

- Rack-free: `tests/test_*.cpp`, run by `./tests/run.sh [filter]`. Uses
  `TEST(name)`, `CHECK(cond)` and `CHECK_EQ(a, b)` from `tests/test.hpp`.
  `CHECK_EQ` prints values with `std::to_string`, so compare
  `duration.count()` (an integer), never `std::chrono` types directly. The
  last line of output is `N tests, M failed checks`. Exemplar:
  `tests/test_param_write.cpp`.
- Rack-linked: `tests/rack/test_*.cpp`, run by `./tests/run-rack.sh [filter]`.
  It needs `plugin.so` (from `./build-wsl.sh`) and `$RACK_DIR/libRack.so`.
  `RACK_DIR` must be the Rack tree the plugin was built against. Exemplar:
  `tests/rack/test_param_interaction.cpp`, which starts with:
  ```cpp
  #include <engine/Engine.hpp>
  #undef PRIVATE
  #include <rack.hpp>

  #include "test.hpp"
  #include "osc/ParamInteraction.hpp"
  ```
  `ChunkedSend` needs no Rack `Context`. Just include
  `"osc/ChunkedSend/ChunkedSend.hpp"` and
  `"osc/Bundler/ChunkedSendBundler.hpp"` after `rack.hpp`.

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Rack-free tests | `./tests/run.sh rtt_estimator` | `... tests, 0 failed checks`, exit 0 |
| Build plugin | `./build-wsl.sh -j8` (no `BENCH=1`) | exit 0 (never run two build wrappers at once) |
| Rack-linked tests | `./tests/run-rack.sh chunked_send` | `... tests, 0 failed checks`, exit 0 |
| All suites | `./tests/run.sh && ./tests/run-rack.sh` | both exit 0 |

## Scope

**In scope** (create only):
- `tests/test_rtt_estimator.cpp`
- `tests/rack/test_chunked_send.cpp`

**Out of scope**:
- Any file under `src/`. If a behavior looks wrong, write the test for the
  *current* behavior, add a `// NOTE:` comment in the test, and list it in your
  report. Don't fix it.
- `ChunkedManager`: it needs a live `OscSender` (sockets and a worker thread).
  Testing it needs a seam, which is a separate decision.
- `tests/run.sh` / `tests/run-rack.sh`.

## Git workflow

- Current branch, or `advisor/006-chunk-transfer-tests` if asked.
- **Do not commit.** The maintainer reviews and commits.

## Steps

### Step 1: RttEstimator tests (Rack-free)

Create `tests/test_rtt_estimator.cpp` with these cases. The expected values
are computed from the formulas above. Use `using namespace std::chrono_literals;`
and compare `.count()` values as `int64_t`.

1. `rtt_estimator_uses_max_rto_before_the_first_sample`:
   `rto(16ms) == MAX_RTO` (200 000 000 ns); `srtt() == 0`.
2. `rtt_estimator_first_sample_sets_srtt_and_half_variation`:
   `addSample(20ms)`; `srtt() == 20ms`; `rto(16ms) == 60ms`
   (20 + max(16, 4×10)).
3. `rtt_estimator_granularity_floors_the_variation_term`:
   `addSample(20ms)`; `rto(50ms) == 70ms` (20 + max(50, 40)).
4. `rtt_estimator_smooths_later_samples`: `addSample(20ms)`,
   `addSample(40ms)` → rttvar 12.5 ms, srtt 22.5 ms; `rto(1ms) == 72.5ms`
   (72 500 000 ns).
5. `rtt_estimator_clamps_to_min_and_max`: one estimator with `addSample(1ms)`
   gives `rto(1ms) == MIN_RTO` (10 ms; the raw value would be 3 ms). A second
   estimator with `addSample(150ms)` gives `rto(1ms) == MAX_RTO`.
6. `rtt_estimator_backoff_doubles_until_a_new_sample`: `addSample(20ms)`,
   `rto(16ms) == 60ms`; `backoff()` → `120ms`; `backoff()` → `200ms`
   (clamped from 240). Then `addSample(20ms)` resets backoffs: rttvar 7.5 ms,
   srtt 20 ms, so `rto(16ms) == 50ms`.
7. `rtt_estimator_backoff_is_capped`: `addSample(1ms)` (base 3 ms with
   `rto(1ms)`); call `backoff()` 5 times → `rto(1ms) == 96ms` (3 × 32); call
   it a 6th time → still `96ms`.
8. `rtt_estimator_reset_forgets_samples`: `addSample(20ms)`, `backoff()`,
   `reset()` → `rto(16ms) == MAX_RTO`, `srtt() == 0`.

**Verify**: `./tests/run.sh rtt_estimator` → `8 tests, 0 failed checks`. If a
case fails, recompute it by hand from the formulas before touching the test.
A mismatch you can't explain is a STOP condition, because it means the formula
differs from the excerpt.

### Step 2: Build the plugin

**Verify**: `./build-wsl.sh -j8` → exit 0 and `plugin.so` exists in the repo
root. Don't pass `BENCH=1`.

### Step 3: ChunkedSend tests (Rack-linked)

Create `tests/rack/test_chunked_send.cpp`. Start from this harness:

```cpp
#include <rack.hpp>

#include "test.hpp"
#include "osc/ChunkedSend/ChunkedSend.hpp"
#include "osc/Bundler/ChunkedSendBundler.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

struct TestSend : ChunkedSend {
  explicit TestSend(int64_t size) : ChunkedSend(new uint8_t[size](), size) {}

  ChunkedSendBundler* getBundlerForChunk(int32_t chunkNum) override {
    return new ChunkedSendBundler(
      "/set/texture/chunk", id, sequenceId, chunkNum, numChunks, chunkSize,
      size, data, 0
    );
  }
};

// a send with exactly three chunks, the last one a single byte
std::unique_ptr<TestSend> threeChunkSend() {
  TestSend probe(1);
  probe.init();
  auto send = std::make_unique<TestSend>(2 * (int64_t)probe.chunkSize + 1);
  send->init();
  return send;
}

std::vector<int32_t> takeFixed(ChunkedSend& send) {
  std::vector<int32_t> due;
  send.takeChunksDue(due, false, 0ns, 0ns);
  return due;
}

void sendAll(ChunkedSend& send, const std::vector<int32_t>& chunks) {
  for (int32_t n : chunks) send.registerChunkSent(n);
}

} // namespace
```

If `#include <rack.hpp>` alone fails to compile, copy the exact include
preamble from `tests/rack/test_param_interaction.cpp`
(`#include <engine/Engine.hpp>`, `#undef PRIVATE`, `#include <rack.hpp>`).

Write these tests. Prefix every name with `chunked_send_`:

1. `chunked_send_init_splits_into_chunks`: `threeChunkSend()` →
   `numChunks == 3`, `chunkSize > 0`, `!sendSucceeded()`, `!sendFailed()`.
2. `chunked_send_takes_pending_chunks_once`: the first `takeFixed` returns
   `{0, 1, 2}`. A second `takeFixed` returns empty, because queued chunks
   aren't taken again.
3. `chunked_send_rejects_invalid_and_duplicate_acks`: `ack(-1)` and `ack(3)`
   are false. Take and send all. `ack(1)` is true, then `ack(1)` again is
   false. `acked(1)` is true, `acked(0)` false, `acked(99)` false.
4. `chunked_send_succeeds_when_every_chunk_is_acked`: take, send all, ack
   0, 1, 2 → `sendSucceeded()`.
5. `chunked_send_dropped_chunk_returns_to_pending`: `takeFixed` (all queued),
   `registerChunkDropped(1)` → next `takeFixed` returns `{1}`.
6. `chunked_send_reports_rtt_only_on_the_completing_ack`: take and send all.
   `std::this_thread::sleep_for(2ms)`. Pass a
   `std::chrono::nanoseconds rtt{0}` to each ack. After acking 0 and 1,
   `rtt.count() == 0`. After acking 2, `rtt >= 2ms`.
7. `chunked_send_excludes_resent_chunks_from_rtt` (Karn): take and send all.
   `sleep_for(3ms)`. `takeChunksDue(due, true, 1ms, 1ms)` returns 3 (first
   timeouts) and `due == {0,1,2}`. `sendAll(due)` again, so sendCount is 2
   everywhere. Ack all with `rtt` → `rtt.count() == 0`.
8. `chunked_send_fixed_mode_waits_for_retry_timeout`: take and send all. An
   immediate `takeFixed` returns empty.
9. `chunked_send_ack_while_queued_for_resend_stays_acked`: take all (queued,
   never sent). `ack(0)` is true. `registerChunkSent(0)` → `acked(0)` still
   true. A later `takeFixed` never includes 0.
10. `chunked_send_adaptive_gives_up_after_limit` (slow, ~1.2 s): take and
    send all. `sleep_for(ChunkedSend::GIVE_UP_AFTER + 20ms)`.
    `takeChunksDue(due, true, 1ms, 1ms)` → `sendFailed()` is true.

**Verify**: `./tests/run-rack.sh chunked_send` → `10 tests, 0 failed checks`.

### Step 4: Run everything

**Verify**: `./tests/run.sh` → `0 failed checks` (45 + 8 = 53 tests at
planning time); `./tests/run-rack.sh` → `0 failed checks`.

## Test plan

This plan *is* the test plan. In summary: 8 Rack-free `RttEstimator` cases and
10 Rack-linked `ChunkedSend` cases (Steps 1 and 3), covering RTO math,
clamping, backoff and reset; chunking; state transitions; invalid and
duplicate ACKs; Karn RTT selection; drop recovery; the fixed-timeout hold;
ack-while-queued; and adaptive give-up.

## Done criteria

- [ ] `./tests/run.sh rtt_estimator` → `8 tests, 0 failed checks`
- [ ] `./tests/run-rack.sh chunked_send` → `10 tests, 0 failed checks`
- [ ] Full `./tests/run.sh` and `./tests/run-rack.sh` exit 0
- [ ] `git status` shows only the two new test files (plus the `plugin.so`/build outputs, which are ignored)
- [ ] `plans/README.md` status row updated

## STOP conditions

- `tests/run-rack.sh` can't run (no `libRack.so` at `$RACK_DIR`). Finish
  Step 1, report Step 3 as BLOCKED, and include the exact error.
- Linking the Rack-linked test fails with undefined `ChunkedSend` /
  `ChunkedSendBundler` symbols (e.g. if the plugin hides symbols). Report
  rather than changing build flags.
- The plugin was built with `BENCH=1`. Rebuild without it. Don't adapt the
  tests to BENCH.
- Any `RttEstimator` expected value disagrees with the code and isn't an
  arithmetic slip on your side.
- A `ChunkedSend` test reveals behavior that contradicts the comments in
  `ChunkedSend.hpp`. Record it with a `// NOTE:` comment asserting the
  *current* behavior, and report it.

## Maintenance notes

- Tests 7 and 10 rely on real sleeps (3 ms and ~1.2 s). If CI flakes, raise
  the margins. Don't remove the cases. A clock-injection seam in `ChunkedSend`
  would make them deterministic, but that's a separate refactor.
- Any change to `RttEstimator` constants (`MIN_RTO`, `MAX_RTO`,
  `MAX_BACKOFFS`) or `ChunkedSend` timeouts will intentionally break these
  tests. Update the expected values together with a PERFLOG entry.
- A future split of `OscReceiver`/`ChunkedManager` should add `ChunkedManager`
  tests (supersede, unknown-ack, tick resend) on top of these, once a
  test seam for `OscSender` exists.
