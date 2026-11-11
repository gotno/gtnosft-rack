# gtnosft-rack

gtnosft-rack is a C++20 VCV Rack plugin. Its ctrl module exposes patch state and
QOI-compressed textures over OSC/UDP; Tx streams audio over UDP.

## Commands

Run these commands from the repository root.

- From WSL, build with `./build-wsl.sh` (not plain `make`). The user builds
  for Windows with `./build-msys.sh`. Both take make arguments, e.g. `-j8`
  or `BENCH=1`.
- Set `RACK_DIR` to the Rack SDK or Rack source tree; `Makefile` defaults to
  `../..` if it is unset.
- Set up a missing libopus dependency with `./build-wsl.sh dep`; this builds
  the vendored Opus source if needed.
- Run the Rack-free C++ suite with `./tests/run.sh`; select a test by
  case-sensitive name substring, for example `./tests/run.sh resyncs`.
- After building on Linux/WSL, run headless Rack widget tests with
  `./tests/run-rack.sh`. These link `libRack.so` without creating a window.
- On pull requests and pushes, CI runs `make plugin-build-lin-x64` and
  `make plugin-build-win-x64` inside the Rack plugin toolchain, plus `make dep`
  and `make dist` on macOS; see `.github/workflows/build-plugin.yml`. Required
  merge checks are configured on the host.

## Project map

- `src/Ctrl.cpp` and `src/osc/` implement ctrl, OSC routes, the sender and
  chunked transfers.
- `src/texture/` contains `Renderer` (offscreen rendering and the overlay
  surrogate cache) and `Catalog` (texture IDs).
- `src/Tx.cpp` and `src/audio/` implement the audio producer, codec and sender.
- `src/bench/` contains optional instrumentation; `tools/bench/` has its clients.
- `tests/` contains Rack-free C++ tests and `tests/rack/` contains headless
  Rack-linked tests; `API.md` documents the network protocol.

## Conventions

- Queue texture renders through `CtrlWidget::enqueueAction`; `CtrlWidget::step()`
  drains the UI-thread action queue. See `src/osc/OscReceiver.cpp`.
- Keep `Tx::process()` free of locks, allocations, codec and network I/O; its
  ring feeds the `AudioStreamer` worker in `src/audio/AudioStreamer.cpp`.
- Add new C++ source directories to the explicit `SOURCES` globs in `Makefile`.
- Add Rack-free tests to `tests/test_*.cpp` using `TEST(...)` from
  `tests/test.hpp`; `tests/run.sh` compiles all test files.
- Add Rack-linked widget tests to `tests/rack/test_*.cpp`; use
  `tests/run-rack.sh` instead of the Rack-free runner.

## Adding a Rack module

1. Define the model with `createModel` in `src/` and declare it in `src/plugin.hpp`.
2. Register it in `src/plugin.cpp` and add a matching module slug to `plugin.json`.

## Adding an OSC request

1. Register the route in `OscReceiver::generateRoutes` in `src/osc/OscReceiver.cpp`;
   enqueue Rack/UI work through the action queue.
2. For replies, follow `src/osc/Bundler/SettingsBundler.cpp` and enqueue the
   bundler through `OscSender`.
3. Document the address and payload in `API.md`.

## Boundaries

- Never: Don't commit; the user reviews and commits.
- Ask first: Ask about design choices before making large changes.
- Never run build wrappers concurrently: both swap the shared `build/` directory
  and use `.build-lock` (`build-common.sh`).
- Never hand-edit generated files in `build/`, `build-wsl/`, `build-msys/`,
  `build-tests/`, `dep/`, `dep-wsl/`, `dep-msys/` or `dist/`; use the relevant
  build or test command instead.
- `API.md` is for API consumers. Document request/response formats and externally observable behavior, including timing, errors, and limitations. Omit internal implementation details and design rationale.
  - For example, "click ACK arrives after release" belongs; explanations of private event state or engine sample counters don't.

## Further reading

- [Benchmark instructions](tools/bench/AGENTS.md): read before performance work.
- [Benchmark usage and metrics](tools/bench/README.md) and
  [optimization history](tools/bench/PERFLOG.md).
