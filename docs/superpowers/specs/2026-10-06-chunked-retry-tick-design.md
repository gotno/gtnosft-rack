# Chunked Retry Tick Design

Supersedes `2026-10-06-chunked-retry-event-loop-design.md`.

## Goal

Remove the detached, per-retry sleeping thread in `ChunkedManager` with the
smallest change that is lifetime-safe, as an interim step toward a selective
repeat sliding window with batched NACKs.

## Design

### Per-chunk state (`ChunkedSend`)

Each chunk tracks its own state, send count, and timestamps, guarded by
`ChunkedSend::statusMutex`:

| State     | Meaning                                         |
|-----------|-------------------------------------------------|
| `Pending` | needs to be enqueued                            |
| `Queued`  | a bundler for it is in the `OscSender` queue    |
| `Sent`    | on the wire; waiting for an ack or a timeout    |
| `Acked`   | done                                            |

- `takeChunksDue(now)` moves `Pending` chunks, and `Sent` chunks whose
  `lastSentAt` is older than `RETRY_TIMEOUT` (200 ms), to `Queued` and returns
  them. A timed-out chunk that has already been sent `1 + MAX_RETRIES` times
  (6) flags the whole send as failed.
- `registerChunkSent` (sender thread, `onBundleSent`) records the actual send
  time, so the timeout is measured from when the chunk was sent rather than
  when it was queued.
- `registerChunkDropped` (`beforeDestroy`) returns a still-`Queued` chunk to
  `Pending` if its bundler is destroyed without sending.
- Acks can arrive in any state and are idempotent; out-of-range chunk numbers
  are ignored.

Only chunks that are actually due are resent. A chunk is never queued twice,
whereas the previous design re-enqueued every unacked chunk each round.

### Scheduling (`ChunkedManager::tick`)

`OSCctrlWidget::step()` calls `ChunkedManager::tick()` once per frame on the UI
thread. `tick()` retires finished or failed sends and enqueues due chunks.
`add()` processes a new send immediately rather than waiting for the next
frame. Retry precision is one UI frame, which is acceptable for a 200 ms
timeout.

### Thread safety and lifetime

- `chunkedSends` holds `std::shared_ptr<ChunkedSend>` and is guarded by
  `chunkedSendsMutex`. `ack()` may be called from the receiver thread.
- Bundler callbacks (`noopCheck`, `onBundleSent`, `beforeDestroy`) capture the
  `shared_ptr` and never touch the manager or its map, so a send (and its
  `data` buffer) outlives its map entry until its queued bundlers are gone.
- No thread or callback captures `ChunkedManager` or `OSCctrlWidget`, so
  teardown order cannot cause use-after-free from pending retries.
- Lock order: `chunkedSendsMutex` → `ChunkedSend::statusMutex` and
  `chunkedSendsMutex` → `OscSender` queue mutex. The sender thread only takes
  `statusMutex`.

## Path to selective repeat with batched NACKs

The per-chunk state is the receiver-agnostic half of selective repeat:

- A window limit becomes a cap on chunks in `Queued` + `Sent` inside
  `takeChunksDue`.
- A batched NACK maps to "move these `Sent` chunks back to `Pending`".
  The timeout then becomes a fallback for lost NACKs.
- `RETRY_TIMEOUT` can become per-send and adaptive (RTT estimated from
  `lastSentAt`/`ackedAt`).

## Alternatives considered

See the superseded event-loop spec (timers on the `OscSender` worker) and
`docs/chunked-retry-strategies.md`. The tick approach was chosen for
simplicity; its main cost is coupling retry precision to the UI frame rate.

## Validation

- Build with `./build-wsl.sh`.
- Unit-level check of the `ChunkedSend` state machine: initial queue, no
  re-queue while queued, dropped bundler returns to pending, timeout-driven
  resend of only unacked chunks, failure after 6 sends, late acks while queued,
  duplicate/invalid acks ignored.
