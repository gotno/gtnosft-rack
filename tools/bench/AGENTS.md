# Notes for coding agents: overlay bench

Read `README.md` here first. It covers usage and what each metric means.
`PERF.md` records what has been optimized, the measured results, and what's
left. These notes cover the workflow and the mistakes made before.

## Building

- From WSL, build with `./build-wsl.sh` from the repo root (not plain `make`).
  Add `BENCH=1` for instrumentation. The user builds the Windows plugin with
  `./build-msys.sh BENCH=1` and runs the benchmarks themselves.
- Switching `BENCH` forces a full rebuild.
- `nm -C plugin.so | grep -c bench::` is 0 for a normal build and nonzero
  for a bench build. After working on bench code, rebuild the normal build so
  you can confirm it still compiles without instrumentation.

## Instrumentation conventions

- Wrap every bench-only statement in `BENCH(...)` (`src/bench/Bench.hpp`).
  Put routes in the `BENCH(...)` block near the top of
  `OscReceiver::generateRoutes`.
- A trace passes through the pipeline's `bench::Stage` values in enum order.
  Spans are pairs of stages in `bench::submit` (`src/bench/Bench.cpp`). If
  you add, rename or move a stage or span, update `STAGES`/`HEADLINE` in
  `overlay_bench.py` and the metrics table in `README.md`.
- A/B toggles (`/bench/overlay_cache`, `/bench/prep_worker`) take an int,
  apply on the UI thread through an action, and reply on `<route>/ack` with
  `BenchToggleAckBundler`. The client sets each toggle at the start of a run,
  turns it back on at the end, and records it in the result's `args`. Add new
  variants the same way so they can be compared within one Rack session.

## Measuring

- **Compare within one Rack session** using the toggles. Results from
  different sessions are not comparable. A laptop on battery throttles every
  stage by about 20%, which once made a good change (the prep worker) look
  like a regression. If stages a change didn't touch move too, suspect the
  environment before the code.
- Tail latencies are only valid with the client running natively on Windows.
  WSL's default UDP buffer cap causes chunk drops and 200 ms retries.
- The user's result files (`win3x_*.json`, gitignored) are the history of
  the optimization work. Leave them in place.

## Testing client changes

- Run `mock_server.py` and always pass `--host 127.0.0.1`. Without `--host`,
  the client discovers the user's live Rack through `/announce` or the WSL
  gateway.
- Run long-lived servers in an async shell and stop them with the shell tool.
  Don't kill them with `kill $PID`.
- python-osc can't build empty blobs, so build those by hand in tests.

## Process

- Don't commit; the user commits.
- Propose changes and ask about design choices before making large ones.
