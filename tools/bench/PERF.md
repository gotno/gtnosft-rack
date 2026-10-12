# Performance log

Optimizations to the texture/overlay pipeline, with measured results. Unless
noted, runs used 3 Scope overlays at 60 fps, height 512, and the client
natively on Windows (Ryzen AI 9 365 laptop with an integrated GPU, plugged in).
The result files are gitignored `win3x_*.json` files in this directory.

Pipeline per request: OSC receive → UI-thread action queue → render (prepare,
draw, readback) → flip and QOI compression → chunked UDP send → client acks.
At this load, about 8–9 ms of the roughly 10–13 ms from request to first send
is `queue_wait`: the wait for Rack's next frame. None of the items below can
remove that.

## Done

| Step | Change | Result (p50 / p95) |
|---|---|---|
| Overlay cache (`0baseline` → `1cached`) | Reuse the overlay surrogate widget tree per module instead of rebuilding it every frame (`Renderer::renderOverlay`). | prepare −85%, `render_total` −11% / −3%. Small end to end. |
| Request coalescing (→ `2deduped`) | A queued `/get/texture` with the same id and size is replaced by the newer sequence id; superseded requests get no reply. | request→first send −10% / −22%; `ctrl_step` p95 −37%. |
| Action queue swap (→ `3qswap`) | `processActionQueue` swaps the queue out under the lock and runs the actions unlocked. | request→all acked −12% p50. Most of the gain is more coalescing. |
| Prep worker (`6inline` vs `6worker`, same session) | Flip and QOI compression moved off the UI thread to one worker in `ChunkedManager`. The flip moved out of `renderPixels`, so `RenderResult` pixels are bottom-up. | `ctrl_step` −33% / −27% (5.2 → 3.5 ms); latency +2% / +4% (a single worker queues overlays one after another). |
| Adaptive resend timeout (`9*_fixed_retry_drop` vs `9*_var_retry_drop`) | RTO from measured round trips instead of a fixed 200 ms (see Ack strategy). | With 1% loss, request→complete p95 233 → 49 ms locally, 247 → 76 ms on Wi-Fi. No-loss runs: ~0 needless resends. |

Lesson: `4prepworker*` vs `3qswap` first looked like a +20% regression. The
laptop had been unplugged, so the whole machine was throttled. Only trust A/B
comparisons from the same session (see `AGENTS.md`).

## Remaining ideas (from the initial audit)

Numbers like #6 are item numbers from the original 10-item audit, which wasn't
kept. #1, #2, #3a (the queue swap), #4, #5 and #8 are done (above).

- **Hold back renders while the send queue is backed up.** Implemented, not
  yet measured: while the prep and send queues hold more than a UI frame of
  work (queued items × smoothed time per item), `/get/texture` requests wait
  in the action queue and keep coalescing. Off with `--no-back-pressure`.
  This is the server-side safety net against the open-loop collapse (see
  Scaling).
- **Supersede stale in-flight sends (#6).** When a newer frame of the same
  texture is ready, drop the older frame's unsent chunks and stop resending
  it. Matters under send backlog (see Scaling), not just loss.
- **Per-frame budget for the action queue (3b).** Always run at least one
  action, then stop when the frame's time remaining drops below a threshold,
  as `FramebufferWidget` does with `getFrameDurationRemaining()`. `step()`
  runs early in the frame, so Rack's own `-1/60` threshold is too loose.
  Leftovers go back to the front of the queue, and deferred texture requests
  keep coalescing. Only matters above ~12–16 renders per frame, which the
  client's in-flight cap already prevents; low priority.
- **Skip unchanged frames (#7).** Hash after readback and reply "unchanged"
  instead of compressing and sending again. Needs a protocol change. Measure
  how often frames are identical first.
- **Async readback with PBOs (#9).** Readback is about 0.3–0.5 ms. It would
  add a frame of latency or a lot of complexity; low priority.
- **`hideChildren` per-child lookup (#10).** Minor.
- **More than one prep worker.** Would remove the worker queue wait (`handoff`
  p95 about 0.5 ms with 3 overlays). Not worth it at 3 overlays.

## Ack strategy

Each chunk is acked individually and resent individually, with every chunk
sent at once and no window. Considered: selective repeat with a sliding window
and batched NACKs.

Measured (`7ack_local`, `7ack_remote` over Wi-Fi to a second machine, each
with and without `--drop 0.01`; 3 overlays, ~20k acks/s):

- Ack handling costs 0.16–0.27 µs per ack, with the receive thread under 1%
  busy. Fewer acks would save nothing. Sending costs more: about 5 µs per
  `sendto`, with the sender thread about 10% busy.
- Wi-Fi carried 225 Mbit/s down and about 19.5k ack packets/s up with no real
  loss. It adds about 8–10 ms of round trip.
- With 1% loss, the fixed 200 ms resend timeout dominated. Mean
  request→complete went from 14 to 112 ms locally and from 22 to 119 ms on
  Wi-Fi. The 334-chunk overlay's p50 went to about 225 ms on both links,
  because nearly every frame loses a chunk.
- The Wi-Fi drop run also showed 385 duplicate acks and `sendto` cost doubling
  (send_all p95 10 ms): probably link congestion in that one run.

Decision: an adaptive resend timeout, with no protocol change
(`RttEstimator`, after RFC 6298):

- `rto = clamp(srtt + max(G, 4·rttvar), 10 ms, 200 ms)`, which is 200 ms
  until the first sample. `G` is the smoothed UI frame interval (each
  interval capped at 100 ms), because resends are only checked once per frame.
- One sample per send: the slowest round trip among its chunks that were
  sent exactly once (Karn's algorithm), taken when the send completes.
- Each resend doubles that chunk's timeout, capped at 200 ms. A chunk fails
  1.2 s after its first send, the same as the old 6 × 200 ms.
- Any UI tick in which a chunk timed out on its first send doubles the
  estimator's timeout, until the next valid sample. Without this, a link that
  got slower after the timeout reached its floor would leave it stuck: every
  chunk would time out, so none would yield a sample (found in review).
- The estimator resets on `/register`.
- `--fixed-retry` (`/bench/adaptive_retry 0`) restores the old behavior for
  A/B runs.

First version (`8local_*`, `8remote_*`; per-chunk samples, no `G` term):

- 1% drop: p95 request→complete fell from 233 to 43 ms locally and from 241
  to 50 ms on Wi-Fi. Locally, retries were exactly the dropped 1%.
- Wi-Fi with no drops: 8.5% of chunks resent needlessly, 18.6k duplicate
  acks, about 20% more sending. SRTT was 9.9 ms but the RTO only 10.1 ms:
  ~130 samples per frame arrive as one tight burst, so the variation collapsed
  and left no margin for frame-to-frame jitter (SRTT p99 70 ms). Fixed with
  one sample per send and the `G` term.

Second version (`9local_*`, `9remote_*`, one Rack session; compare only
within it, since the Wi-Fi round trip was slower than in run 8: SRTT ~12.5 vs
~9 ms):

- RTO about 20 ms locally and 30 ms on Wi-Fi.
- Wi-Fi with no drops: 0.15% resent and 292 duplicate acks (was 8.5% and
  18.6k); upstream packets -14%, sender busy -19%. Locally: 0 resends.
- 1% drop, fixed → adaptive: p95 request→complete 233 → 49 ms locally and
  247 → 76 ms on Wi-Fi, with resends equal to the dropped 1%.
- Every frame with a drop backs the timeout off once (~500 backoffs per drop
  run), which adds some recovery time. Acceptable for now; a possible
  refinement is to back off only when many in-flight chunks time out at once.
- The fixed-timeout Wi-Fi drop run again showed duplicate acks (168) and the
  sender nearly twice as busy, as in `7ack_remote_drop`. Unexplained.

Not done: NACKs plus a completion ack (would remove the upstream ack traffic
and recover a little faster) and a window or pacing. Revisit if the real
client shows loss or upstream trouble. Combine with #6 so stale overlay
frames aren't resent.

## Scaling: many overlays and the in-flight cap

`8x_remote`: 8 overlays (6 Scopes, Tangle, Bloom) at 60 fps open loop over
Wi-Fi.

- Rack ran at ~54 fps in this patch even while idle, and each texture renders
  at most once per frame. A 60 Hz client therefore always gets some requests
  coalesced: 27% were superseded, and each overlay averaged 43.5 fps.
- UI-thread cost scales linearly with the number of renders per frame:
  `ctrl_step` was 8.7 ms p50 for about 6.4 renders per frame (3.6 ms with 3
  overlays). Rack's own frame time didn't change.
- Per-overlay fps fell in request order (48 → 38). The client sends all 8
  requests together, and when a frame starts in the middle of that burst, the
  later overlays wait a frame and get coalesced. The 3-overlay runs show it
  too. Pacing on the client (the in-flight cap) removes it.
- 100 Mbps down; 0.57% resends, all of them unnecessary.

`16x_*`: 16 Scopes, one Rack session. `--max-in-flight N` schedules requests
like the headset client (see `README.md`). Wi-Fi runs use the default
`--rate 60` as a per-overlay limit.

| Run | fps per overlay | Total fps | Rack fps | `ctrl_step` p50 / p95 (ms) | Request→first chunk p50 / p95 (ms) | Mbps down | Resent |
|---|---|---|---|---|---|---|---|
| `local_mif16_rate0` | 54.0 | 857 | 54.8 | 8.7 / 10.5 | 15.7 / 29 | 280 | 0.23% |
| `remote_mif2` | 6.7 | 108 | 55.4 | 2.2 / 3.2 | 16.2 / 30.5 | 35 | 0% |
| `remote_mif4` | 12.9 | 206 | 58.1 | 3.1 / 4.3 | 16.8 / 31 | 67 | 0.19% |
| `remote_mif8` | 21.2 | 336 | 59.0 | 3.9 / 6.1 | 21.1 / 41 | 110 | 0.27% |
| `remote_mif16` | 34.0 | 537 | 58.9 | 5.3 / 8.9 | 26.7 / 48.5 | 175 | 0.41% |
| `remote_open` | 38.1 | 600 | 57.2 | 7.4 / 10.5 | 1072 / 2425 | 256 | 24% |

- **Open loop over Wi-Fi collapsed.** `send_queue` was ~1 s p50 and 2.4 s
  p95, and the sender thread was busy 76% of the time. SRTT p95 reached 171 ms
  as acks queued up on the Wi-Fi link, so 58k resends fired; every one was a
  duplicate. Nothing pushes back on the sender: coalescing only happens before
  rendering, so Rack kept rendering ~650 frames/s into a link that couldn't
  carry them. 8 overlays open loop (100 Mbps) was fine; somewhere between
  ~175 and ~250 Mbps this link stopped keeping up.
- **The in-flight cap prevents that.** A request waits for its first chunk,
  and a backed-up send queue delays that first chunk, so the client slows
  down on its own. Fairness across overlays was even.
- **Each slot handles about one request per request→first-chunk time**
  (total fps per slot: 54, 51, 42, 34 as N rises). The wait for the next UI
  frame stayed at ~9–11 ms, but the network's share grew from ~6.5 ms to
  ~15 ms with load. Latency-based adaptation on the client sees a clear
  signal, but from the network, not from deferred frames.
- **UI threshold.** 16 renders per frame (local) took 8.7 ms of `ctrl_step`
  and pulled Rack from 60 to ~55 fps (`frame.interval` p95 17 → 29 ms). With
  the cap, Rack stayed at ~59 fps over Wi-Fi. A UI frame budget only matters
  above ~12–16 renders per frame.
- `render_total` was higher at low load (0.91 ms at N=2 vs 0.48 ms at N=16),
  probably because the GPU clocks down when lightly loaded.

Conclusions: the client needs the in-flight cap (4–8 on Wi-Fi). On the
server, a safety net should watch the send backlog rather than UI time.
