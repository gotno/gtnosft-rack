# overlay bench

Measures the texture/overlay render round trip: from the moment a request is
received to when its first chunk goes out and the client acks it. It also
records Rack's frame time, per-texture stream fps, and overlay cache stats.

## 1. Build OSCctrl with instrumentation

```sh
./build-msys.sh BENCH=1     # Windows build
./build-wsl.sh BENCH=1      # Linux build
```

Instrumentation only exists in a `BENCH=1` build. A normal build leaves it out
entirely (the `BENCH(...)` macro expands to nothing) and ignores the `/bench/*`
routes. Switching `BENCH` between builds forces a full rebuild.

## 2. Set up the client

Run the client natively on the same OS as Rack (PowerShell for Windows Rack).
Under WSL2, the UDP receive buffer is capped (about 208 KB by default) and the
NAT adds overhead. Large frames then lose chunks, and the slowest frames take
hundreds of ms because of the server's 200 ms chunk retries. That measures the
client, not the plugin.

Windows (PowerShell):

```powershell
cd tools\bench
python -m venv .venv-win
.venv-win\Scripts\activate # to enter the python venv shell (deactivate to exit)
pip install -r requirements.txt
python overlay_bench.py list
```

Linux / WSL:

```sh
cd tools/bench
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
sudo sysctl -w net.core.rmem_max=8388608   # if the client warns about the buffer cap
```

The examples below use `.venv/bin/python`; on Windows use
`.venv-win\Scripts\python`.

## 3. Prepare a patch

Save a fixed bench patch so runs are comparable, for example a Scope fed by an
LFO with the Scope's inputs cabled. Open it in Rack and add an OSCctrl module.

## 4. Run

```sh
.venv/bin/python overlay_bench.py run --no-overlay-cache --label baseline --json before.json
.venv/bin/python overlay_bench.py run --label cache --json after.json
.venv/bin/python overlay_bench.py compare before.json after.json [--all]
```

`--no-overlay-cache` turns off the overlay surrogate cache for that run, so
every frame rebuilds its surrogate as it did before the cache existed.
`--inline-prep` does the flip and compression on the UI thread instead of the
prep worker thread. `--fixed-retry` resends unacked chunks after a fixed
200 ms, as before the RTT-based timeout existed. `--no-back-pressure` renders
texture requests even while the server's prep and send queues hold more than
a UI frame of work (by default they wait and keep coalescing).
`--no-supersede` keeps sending (and resending) an older frame after a newer
frame of the same texture and size is ready. `--no-chunk-pacing` turns off
pacing of texture chunks for a managed bCast audio stream. The client sets all six states at the start of every run and turns them back on when the run finishes, so all variants can use the same
`BENCH=1` build. Prefer comparing variants within one Rack session: power
state (e.g. a laptop on battery) shifts every stage between sessions.

A run goes through these steps:

1. Register.
2. Reset the server stats.
3. Find the target modules (`--plugin/--module/--index` or `--module-id`;
   default `Fundamental:Scope`).
4. Record an idle baseline (`--idle`, 3 s).
5. Warm up (`--warmup`, 1 s).
6. Reset the stats again.
7. Stream overlay requests (`--rate` fps open loop, default 60, or `--rate 0`
   for closed loop, or scheduled with `--max-in-flight`; `--duration` 10 s).
8. Drain, then print and save the report.

### Several overlays at once

```sh
.venv/bin/python overlay_bench.py run --module-id 12 34 56 --label three
```

`--module-id` takes one or more ids and can be repeated. `overlay_bench.py
list` prints the patch's modules and their ids. Every listed
overlay streams at the same time:

- `--rate` is per overlay. Open loop requests all the overlays together on
  each tick.
- With `--rate 0`, each overlay requests its next frame as soon as its own
  previous frame finishes.
- `--max-in-flight N` schedules requests the way the headset client does.
  An overlay may request again once the first chunk of its newest frame has
  arrived (or `--timeout` passed) and `1/--rate` has passed since its last
  request (`--rate 0` removes that cap). At most `N` requests may be waiting
  for their first chunk at once. Free slots go to the overlays that have
  waited longest since their last request. Requests are never coalesced in
  this mode, and `client.request_to_first_chunk` is the latency the headset
  client adapts from.

The run prints a per-overlay table of fps and latency. `client.fps` is the
average per overlay, so you can compare runs with different overlay counts, and
`compare` shows the overlay count next to each label.

Size is set with `--height` (default 512), optionally with `--width`, or with
`--scale`.

### Networking

- The client binds UDP **7746** (OSCctrl's fixed TX port). Only one client can
  be registered at a time, so close any other client first. If another client
  stopped without disconnecting, wait about 5 s for its keepalive to time out.
- The client finds the host from `/announce` broadcasts. If none arrive (for
  example under WSL2 NAT), it falls back to `--host`, then to the WSL default
  gateway, which is the Windows host. The server port defaults to 7225
  (`--port`).
- The Windows firewall must allow inbound UDP to the WSL client. To include real
  network cost, run the client from another machine.
- On Linux, the client reports `client.socket_drops`: datagrams the kernel
  dropped because the receive buffer was full during the measured stream. If
  it's nonzero, the tail latencies and chunk retries are partly the client's
  fault.

### Transport and loss

These measure what the chunk/ack protocol costs, as groundwork for deciding
whether to change it (see `PERFLOG.md`).

- **Over a real network:** run the client on another machine with
  `--host <rack machine's IP>`, e.g. a second laptop on the same Wi-Fi. On
  localhost nothing is ever lost, so retries stay at 0 and the ack path is
  exercised only at its cheapest.
- **Simulated loss:** `--drop P` discards that fraction of incoming chunk
  packets without acking them, and `--drop-acks P` skips that fraction of acks
  (`P` in 0–1, e.g. `--drop 0.01`). Both act after the packet reaches the
  client, so the server resends exactly as it would for real loss.
  `compare` notes the rates next to each label.

The run prints a `== transport ==` section, saved as `transport` in the JSON;
see the metrics below.

### Testing the client without Rack

`mock_server.py` stands in for OSCctrl with canned stats and small fake
frames. Use it to check client changes. The numbers it produces are fake.

```sh
.venv/bin/python mock_server.py &
.venv/bin/python overlay_bench.py run --host 127.0.0.1 --duration 1 --idle 0.3 --warmup 0
```

Always pass `--host 127.0.0.1`. Otherwise the client may find a live Rack
first and send its toggles there.

## Metrics

Server spans are named `<kind>.<metric>`. `kind` is one of:

- `overlay_hit`: the surrogate came from the cache.
- `overlay_miss`: the surrogate was built for this request.
- `texture`: a non-overlay render.

| metric | from → to |
|---|---|
| `queue_wait` | received on the OSC thread → dequeued on the UI thread |
| `prepare` | render start → surrogate/framebuffer ready |
| `draw` | prepared → `glFinish` after the draw |
| `readback` | drawn → `glReadPixels` done |
| `handoff` | readback → flip/compress starts (prep worker queue wait) |
| `flip` | vertical flip |
| `compress` | QOI compression |
| `send_queue` | compressed → first chunk sent |
| `send_all` | first → last chunk sent |
| `ack_all` | first chunk sent → all chunks acked |
| `render_total` | render start → readback done |
| `request_to_first_send` | received → first chunk sent (headline) |
| `request_to_all_acked` | received → all chunks acked |
| `raw_kb`, `compressed_kb`, `chunks` | payload sizes |

The flip and compression moved to a prep worker thread after the
`win3x_3qswap` run. In results recorded before that, `render_total` includes
the flip, and `handoff` means flip → compression start.

Other metrics:

- `frame.interval`: Rack's frame start-to-start time, including vsync. Compare
  the idle and stream values to see how much streaming costs the UI.
- `frame.ctrl_step`: time spent in OSCctrl's `step()`, which includes the
  renders.
- `client.*`: latencies measured by the client. These include the network.
- Counters:
  - `overlay_cache.hit/miss/evict_*`
  - per-kind `requests/completed/failed/superseded/retries/rerenders`
    (`superseded`: sends stopped for a newer frame of the same image)
  - `texture_requests.coalesced` (server): requests merged into an
    already-queued request for the same texture id and size. Only the newest
    sequence id is rendered and sent; older ones get no reply.
  - `texture_requests.deferred` (server): frames a texture request waited
    because the prep and send queues held more than a UI frame of work. One
    request waiting three frames counts three.
  - `chunked.superseded` (server): in-flight sends stopped because a newer
    frame of the same texture id and size was ready. Their unsent chunks are
    skipped and they're never resent.
  - `client.superseded`: requests that never completed because a later frame
    of the same overlay got a reply instead: either the server coalesced the
    request (no reply), or, with supersede on, stopped sending it partway.
  - `client.incomplete` (frames that timed out)
  - `client.duplicate_chunks` (chunks received more than once, i.e. resent
    after a lost ack or a slow client)
- Gauges: `overlay_cache.entries/bytes` and `prep_queue.depth` (sends waiting
  for the prep worker), each with a `.peak`.
- Thread tallies (server counters `<name>` = calls and `<name>.busy_us` = total
  time inside, both lock-free):
  - `rx.packets`: every packet on the OSC receive thread.
  - `rx.acks`: `/ack_chunk` handling, including waiting on the chunk map lock
    the UI thread holds during `tick()`. `rx.acks.unknown` counts acks for
    sends that had already finished, and `rx.acks.duplicate` counts acks for
    chunks already acked.
  - `tx.packets`: the socket send call on the sender thread.
  - `ui.chunk_tick`: `ChunkedManager::tick()` on the UI thread (resend scan).
  - `retry.backoffs`: ticks in which a chunk timed out on its first send, so
    the resend timeout was doubled (count only).
  - `ui.action.<tag>`: UI-thread actions by the request that queued them
    (`texture`, `texture.deferred`, `module_structure`, `module_state`,
    `heartbeat`, ...). Shows what the action queue spends its time on.
  - `tx.bundler.<name>`: sends of non-chunk bundlers (everything on the
    priority queue) by bundler name, on the sender thread.
- Action queue, recorded only in frames that ran actions:
  - `ui.actions_per_frame` and `ui.actions_ms` (time spent running them)
  - `ui.action_wait` (ms): the longest enqueue → run wait in that frame
  - gauge `ui.action_queue.depth` (actions waiting at the start of the
    frame, including any carried over), with `.peak`
  - counter `ui.action_budget.exceeded`: frames that stopped early because
    they used up `ACTION_BUDGET` (8 ms) and carried the rest to the next
    frame. To compare with running the whole queue in one frame, turn off
    `/bench/action_budget` (int 0), or untick "Budget UI actions per frame"
    in the OSCctrl menu.
- `tx.priority_wait` (ms): non-chunk bundler enqueue → send start. Gauge
  `tx.priority_queue.depth`, with `.peak`.
- Server heartbeats (direct mode, from the heartbeat timer):
  - `heartbeat.gap` (ms): time between consecutive heartbeat sends. Nominally
    `HEARTBEAT_INTERVAL_MS`; this is what the client's timeout sees.
  - `heartbeat.latency` (ms): timer fired → sent. Split into
    `heartbeat.ui_hop` (fired → reached the sender queue, i.e. the UI action
    queue) and `heartbeat.send_wait` (sender queue → sent).
  - counters `heartbeat.client_missed` (server-side misses of the client's
    `/keepalive`) and `heartbeat.client_gone` (client dropped). When the
    server drops the client, the report is written to the Rack log
    automatically.
  - Heartbeats are sent straight from the heartbeat timer thread. To compare
    with the old path through the UI action queue, turn off
    `/bench/heartbeat_direct` (int 0), or untick "Heartbeats skip the UI
    queue" in the OSCctrl menu. `heartbeat.ui_hop` only means something with
    it off.
- Module widgets built for `/get/module_structure` (and panel renders):
  - tallies `connected_widget.create_module`, `.create_widget`, and
    `.add_module` (adding the module to the engine, which takes the engine's
    exclusive lock), and `module_structure.delete_widget` (which includes
    removing the module from the engine).
  - `module_structure.build_ms`: the whole bundler build. Builds over 50 ms
    are logged with their plugin and module slugs.
- The OSCctrl context menu (bench builds) has "Reset bench" and "Dump bench
  report" (to the Rack log), for measuring with any client, e.g. resetting
  before connecting to a large patch and dumping after the initial burst of
  requests.
- `retry.srtt` and `retry.rto` (ms, sampled once per UI frame): the smoothed
  round trip (one sample per completed send: its slowest chunk) and the
  resulting resend timeout. The estimator also runs with
  `--fixed-retry`; the timeout just isn't used then.
- `pipeline.backlog` (ms, sampled once per UI frame): estimated time to drain
  the prep and send queues. Each queue counts queued items × smoothed time
  per item; the send queue uses its oldest item's wait instead when that's
  longer, so socket stalls count. Texture renders wait while it exceeds the
  UI frame interval.
- Client transport counters for the stream: `client.chunk_packets/bytes`
  received, `client.acks_sent/ack_bytes`, and
  `client.sim_dropped_packets/acks`.
- `transport.*` (derived, single values; `compare` shows them in the p50
  column): acks/s, acks per completed frame, µs per ack, receive-thread busy
  %, send-call cost and sender busy %, chunk tick µs, smoothed RTT and resend
  timeout (p50 over frames), `pipeline.backlog` p95, deferred request-frames, superseded sends,
  chunks resent as % of packets sent, downstream Mbit/s, and upstream (ack) kbit/s and packets/s, plus the chunk pacing rate
  (`pacing_rate_mbps`, 0 = unlimited) and median pacing wait (`pacing_wait_ms`).
  Server rates use the server's report window; client rates use the client's
  stream time. Both include the drain.
- Per-texture fps: completed sends over the report window.

## Audio bench

`audio_bench.py` tests a bCast audio stream without Unreal. It receives the
stream the way RackSim does, on its own socket and thread, and measures
arrival timing, loss, reordering, clock drift and what a fixed-target jitter
buffer at the preset's target would have done. Opus presets need `opuslib` and
the libopus shared library (`--wav` and `--analyze` decode them).

```sh
.venv/bin/python audio_bench.py run --duration 10 --json before.json
.venv/bin/python audio_bench.py compare before.json after.json
```

- **Passive** (`--passive`): only listens. Point a standalone bCast at this
  machine's IP, port 7747 (`--audio-port` changes it).
- **Managed** (default): connects to OSCctrl like `overlay_bench.py` (same
  `--host`/`--port` discovery), finds the bCast placed directly next to it,
  optionally selects `--preset ID`, enables the stream to this machine, and
  sends `/audio/stats` from the simulated buffer. It disables the stream when
  the run ends.
- **Overlays at the same time:** `--overlays N` streams N overlays
  (`--overlay-height`, `--overlay-rate`) during the measurement. Run it once
  with and once with `--no-chunk-pacing`, then `compare`, to see what pacing
  the texture chunks does to the audio. Needs a `BENCH=1` build.
- **Glitch check:** `--analyze` decodes the last epoch and looks for glitches
  and dropouts in the left channel. Patch a VCO sine into bCast.
- **Recording:** `--wav PATH` writes the decoded audio.
- **Simulated loss:** `--drop P` discards that fraction of audio packets after
  they arrive.

| Metric | Meaning |
|---|---|
| `jitter_ms` | arrival time minus the frame's media time, relative to the minimum |
| `max_gap_ms` | largest time between packets |
| `lost` / `recovered` | frames never received / received only as a redundant copy |
| `reordered` / `duplicates` | packets older than the newest seen / repeated seq |
| `epoch_changes` | stream resets |
| `drift_ppm` | sender clock rate relative to ours |
| `sim_underruns` / `sim_lost` | what a fixed-target buffer at the preset target would have done |
| `server.pacing_rate_mbps` | chunk pacing rate at the end (0 = unlimited) |
| `server.client_underruns` | last reported `/audio/stats` underruns |

To test without Rack, run `mock_server.py --audio` (managed) or
`mock_server.py --audio-to 127.0.0.1:7747` (passive). Both send a 1 kHz PCM
sine. In managed mode pass `--host 127.0.0.1`.
