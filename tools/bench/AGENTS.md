# Benchmark tooling

Python clients and analysis for ctrl and Tx; see the [root instructions](../../AGENTS.md).
Read `README.md` here first. It covers usage and what each metric means.
`PERFLOG.md` records what has been optimized, the measured results, and what's
left. These notes cover the workflow and the mistakes made before.

## Commands

Run these commands from `tools/bench/` unless stated otherwise.

- On Linux/WSL, set up missing dependencies with `python3 -m venv .venv` and
  `.venv/bin/pip install -r requirements.txt`; see `README.md` for Windows.
- Run `python3 -m unittest -v test_audio_analysis`; select one case with
  `python3 -m unittest -v test_audio_analysis.PacketTests.test_round_trip`.
- For an offline overlay smoke test, start `.venv/bin/python mock_server.py --port 17225`
  (long-running), then after it is ready run `.venv/bin/python overlay_bench.py run
  --host 127.0.0.1 --port 17225 --duration 1 --idle 0.3 --warmup 0`.
  The client binds UDP 7746 on all interfaces; check that it is free first.
- Test `audio_bench.py` with `mock_server.py --audio` (managed) or
  `--audio-to 127.0.0.1:7747` (passive). Always pass `--host 127.0.0.1` in
  managed mode.
- The user builds the Windows plugin with `./build-msys.sh BENCH=1` and runs
  the benchmarks themselves (build from the repository root).
- Switching `BENCH` forces a full rebuild. After working on bench code, rebuild
  the normal build so you can confirm it still compiles without instrumentation.
- From the repository root on Linux, `nm -C plugin.so | grep -c 'bench::'` is 0
  for a normal build and nonzero for a bench build; `grep` exits 1 when it is 0.

## Instrumentation conventions

- Guard benchmark-only production call sites with `BENCH(...)` from
  `../../src/bench/Bench.hpp`; dedicated benchmark implementation files use
  `GTNOSFT_BENCH` guards.
- Put routes in the `BENCH(...)` block of `OscReceiver::generateRoutes` in
  `../../src/osc/OscReceiver.cpp`.
- A trace passes through the pipeline's `bench::Stage` values in enum order.
  Spans are pairs of stages in `bench::submit` (`../../src/bench/Bench.cpp`). If
  you add, rename or move a stage or span, update `STAGES`/`HEADLINE` in
  `overlay_bench.py` and the metrics table in `README.md`.
- Accept an integer for each A/B toggle, apply the change through a UI-thread
  action, and reply using `BenchToggleAckBundler` on the route's `/ack` suffix.
- When adding a client toggle, update `TOGGLE_ROUTES`, the separate setup/restore
  loop and CLI arguments in `overlay_bench.py`; the client restores disabled
  toggles and records its arguments. Not every server toggle is wired into the
  clients.

## Measuring and testing

- **Compare within one Rack session** using the toggles. Results from
  different sessions are not comparable. A laptop on battery throttles every
  stage by about 20%, which once made a good change (the prep worker) look
  like a regression. If stages a change didn't touch move too, suspect the
  environment before the code.
- Tail latencies are only valid with the client running natively on Windows.
  WSL's default UDP buffer cap causes chunk drops and retries.
- Without `--host`, the client discovers the user's live Rack through
  `/announce` or the WSL gateway. Stop the mock server you started after testing.
- python-osc can't build empty blobs, so build those by hand in tests.
- Run `test_audio_analysis.py` after changing `audio_analysis.py`.

## Boundaries

- Never: The user's result files (`win3x_*.json`, gitignored) are the history of
  the optimization work. Leave them in place.
- Never: Don't commit; the user commits.
- Ask first: Propose changes and ask about design choices before making large ones.

## Further reading

- [Usage and metrics](README.md) and [optimization history](PERFLOG.md).
