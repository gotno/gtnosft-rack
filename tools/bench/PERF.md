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

## Ack strategy (under evaluation)

Today each chunk is acked individually and resent individually after 200 ms
(`ChunkedSend::RETRY_TIMEOUT`), with every chunk sent at once and no window.
Considered: selective repeat with a sliding window and batched NACKs.

On localhost (`win3x_6worker`) the wire accounts for about 2 ms of a 13.5 ms
request→all-acked time, with 0 retries, so a protocol change would gain little
there. Its potential wins are fewer acks (about 21k/s with 3 overlays, each a
map lookup and two mutexes on the receive thread) and recovering from loss in
one round trip instead of 200 ms. Both only matter on a lossy link.

Preferred shape if the numbers justify it: no window, one completion ack per
frame, a NACK bitmap for gaps, and one fallback timer per send on the server.
Combine it with #6 so stale overlay frames aren't resent. Add a window or
pacing only if burst drops show up.

Tooling to decide: the `== transport ==` metrics (receive-thread busy %, acks
per frame, resend %), `--drop`/`--drop-acks` loss simulation, and runs from a
second machine over Wi-Fi. See README "Transport and loss".

## Known issues (not performance)

- `/get/texture` doesn't handle an `Empty` `RenderResult`, which happens when
  the texture id is unknown.
- `API.md`'s texture section is out of date.
