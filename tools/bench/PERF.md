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

Lesson: `4prepworker*` vs `3qswap` first looked like a +20% regression. The
laptop had been unplugged, so the whole machine was throttled. Only trust A/B
comparisons from the same session (see `AGENTS.md`).

## Remaining ideas (from the initial audit)

Numbers like #6 are item numbers from the original 10-item audit, which wasn't
kept. #1, #2, #3a (the queue swap), #4, #5 and #8 are done (above).

- **Per-frame budget for the action queue (3b).** Always run at least one
  action, then stop when the frame's time remaining drops below a threshold,
  as `FramebufferWidget` does with `getFrameDurationRemaining()`. `step()`
  runs early in the frame, so Rack's own `-1/60` threshold is too loose.
  Leftovers go back to the front of the queue, and deferred texture requests
  keep coalescing. It only shows a benefit under bursts, so it needs a burst
  mode in the client. Deferred by the user.
- **Supersede stale in-flight sends (#6).** Only helps when chunks are lost or
  the client is slow; Windows tails are already about 30 ms.
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

- `rto = clamp(srtt + 4·rttvar, 10 ms, 200 ms)`, which is 200 ms until the
  first sample. The UI tick only resends once per frame anyway.
- Samples come only from chunks sent exactly once (Karn's algorithm).
- Each resend doubles that chunk's timeout, capped at 200 ms. A chunk fails
  1.2 s after its first send, the same as the old 6 × 200 ms.
- Any UI tick in which a chunk timed out on its first send doubles the
  estimator's timeout, until the next valid sample. Without this, a link that
  got slower after the timeout reached its floor would leave it stuck: every
  chunk would time out, so none would yield a sample (found in review).
- The estimator resets on `/register`.
- `--fixed-retry` (`/bench/adaptive_retry 0`) restores the old behavior for
  A/B runs.

Not done: NACKs plus a completion ack (would remove the upstream ack traffic
and recover a little faster) and a window or pacing. Revisit if the real
client shows loss or upstream trouble. Combine with #6 so stale overlay
frames aren't resent.

## Known issues (not performance)

- `/get/texture` doesn't handle an `Empty` `RenderResult`, which happens when
  the texture id is unknown.
- `API.md`'s texture section is out of date.
