# Notes for coding agents

gtnosft-rack is a VCV Rack plugin. Its OSCctrl module exposes the patch over
OSC/UDP: modules, params and lights, plus rendered textures (panels, overlays,
knobs and so on) streamed to a client as QOI-compressed chunks.

## Workflow

- From WSL, build with `./build-wsl.sh` (not plain `make`). The user builds
  for Windows with `./build-msys.sh`. Both take make arguments, e.g. `-j8`
  or `BENCH=1`.
- Don't commit; the user reviews and commits.
- Ask about design choices before making large changes.

## Map

- `src/OSCctrl.cpp`: the module and widget. `OSCctrlWidget::step()` runs the
  UI-thread action queue that every render goes through.
- `src/osc/`: receiver routes (`OscReceiver.cpp`), the sender thread, and
  chunked sends with retries and acks (`ChunkedManager`, `ChunkedSend/`).
- `src/texture/`: `Renderer` (offscreen renders and the overlay surrogate
  cache) and `Catalog` (texture ids).
- `API.md`: the OSC protocol. Its texture section is out of date; the code is
  authoritative.

## Performance and benchmarking

- `tools/bench/AGENTS.md`: how to build, measure and test the bench tooling
  safely. Read it before any performance work.
- `tools/bench/PERF.md`: optimizations made so far, their measured effect, and
  what's left.
- `tools/bench/README.md`: how to use the bench client and what each metric
  means.
