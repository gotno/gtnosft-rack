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
every frame rebuilds its surrogate as it did before the cache existed. The
client sets the cache state at the start of every run and turns the cache back
on when a no-cache run finishes. Both runs can therefore use the same
`BENCH=1` build.

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
| `flip` | readback → vertical flip done |
| `handoff` | flip → compression start |
| `compress` | QOI compression |
| `send_queue` | compressed → first chunk sent |
| `send_all` | first → last chunk sent |
| `ack_all` | first chunk sent → all chunks acked |
| `render_total` | render start → flipped |
| `request_to_first_send` | received → first chunk sent (headline) |
| `request_to_all_acked` | received → all chunks acked |
| `raw_kb`, `compressed_kb`, `chunks` | payload sizes |

Other metrics:

- `frame.interval`: Rack's frame start-to-start time, including vsync. Compare
  the idle and stream values to see how much streaming costs the UI.
- `frame.ctrl_step`: time spent in OSCctrl's `step()`, which includes the
  renders.
- `client.*`: latencies measured by the client. These include the network.
- Counters:
  - `overlay_cache.hit/miss/evict_*`
  - per-kind `requests/completed/failed/retries/rerenders`
  - `client.incomplete` (frames that timed out)
  - `client.duplicate_chunks` (chunks received more than once, i.e. resent
    after a lost ack or a slow client)
- Gauges: `overlay_cache.entries/bytes`, each with a `.peak`.
- Per-texture fps: completed sends over the report window.
