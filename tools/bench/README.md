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
prep worker thread. The client sets both states at the start of every run and
turns them back on when the run finishes, so all variants can use the same
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
   for closed loop; `--duration` 10 s).
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
whether to change it (see `PERF.md`).

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
  - per-kind `requests/completed/failed/retries/rerenders`
  - `texture_requests.coalesced` (server): requests merged into an
    already-queued request for the same texture id and size. Only the newest
    sequence id is rendered and sent; older ones get no reply.
  - `client.superseded`: requests that got no reply because the server
    coalesced them (a later frame of the same overlay arrived instead).
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
- Client transport counters for the stream: `client.chunk_packets/bytes`
  received, `client.acks_sent/ack_bytes`, and
  `client.sim_dropped_packets/acks`.
- `transport.*` (derived, single values; `compare` shows them in the p50
  column): acks/s, acks per completed frame, µs per ack, receive-thread busy
  %, send-call cost and sender busy %, chunk tick µs, chunks resent as % of
  packets sent, downstream Mbit/s, and upstream (ack) kbit/s and packets/s.
  Server rates use the server's report window; client rates use the client's
  stream time. Both include the drain.
- Per-texture fps: completed sends over the report window.
