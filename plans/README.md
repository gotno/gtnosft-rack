# Improvement plans

Advisor audit of gtnosft-rack at commit `e444a8f` (2026-11-12, standard
effort). The tree was clean, and `./tests/run.sh` passed 45 tests. Every plan is
self-contained. Run its drift check first. **Executors never commit.** The
maintainer reviews and commits (see `AGENTS.md`).

The maintainer selected all 8 findings. The route rename is no longer in
progress, so plan 001 finishes it rather than waiting on it.

## Execution order and status

| # | Plan | Category | Priority | Effort | Risk | Depends on | Status |
|---|---|---|---|---|---|---|---|
| 001 | [Finish the OSC route rename](001-finish-route-rename.md) | correctness / docs | P1 | S | LOW (wire-visible) | none | TODO |
| 002 | [Stop the heartbeat before OscReceiver teardown](002-stop-heartbeat-before-teardown.md) | correctness | P1 | S | LOW | none | TODO |
| 003 | [Make OscSender's send mode atomic](003-atomic-send-mode.md) | correctness | P2 | S | LOW | none | TODO |
| 004 | [Validate `/get/texture` scale and size](004-validate-texture-args.md) | robustness | P2 | S | LOW | 001 (soft: both edit `API.md`) | TODO |
| 005 | [Run tests in CI and gate the toolchain release build](005-tests-in-ci-and-toolchain-build.md) | DX / CI | P2 | M | LOW | 001 (address-check script) | TODO |
| 006 | [Characterization tests for RttEstimator and ChunkedSend](006-chunk-transfer-tests.md) | tests | P2 | M | LOW | none | TODO |
| 007 | [Characterize EngineClock teardown](007-engine-clock-teardown.md) | correctness (investigate) | P3 | M | MED | none | TODO |
| 008 | [Remove dead chunk code](008-remove-dead-chunk-code.md) | tech debt | P3 | S | LOW | after 006 (both touch `ChunkedSend`) | TODO |

Status values: `TODO`, `IN PROGRESS`, `DONE`, `BLOCKED (<reason>)`, `STALE`.

## Dependency graph

```
001 ──► 004 (soft, API.md)
001 ──► 005 (tools/check_api_addresses.py)
006 ──► 008 (ChunkedSend)
006 ──► any future OscReceiver / ChunkedManager refactor
002, 003, 007 are independent
```

Plans 002, 003 and 006 can run in parallel with 001. Never run two build
wrappers at once, though (`build-wsl.sh`, `build-msys.sh`, or the
toolchain's `build.sh`). They share `build/` and `.build-lock`.

## Notes and assumptions to confirm

- **Plan 001** assumes the canonical ACK form is `/ack/<action>/param`
  (`/ack/click/param`, etc.), matching `API.md`, and not `/act/<action>/param/ack`.
- **Plan 004** picks caps of scale ≤ 16 and width/height ≤ 8192. These are
  well above any real client request. Adjust the constants if you prefer.
- **Plan 005** (revised): releases are built locally with
  `../rack-plugin-toolchain/build.sh`, so no Docker image is pinned. The
  plan rewrites `build.sh` to run the test gate (`./build-wsl.sh` plus all four
  test runners) before cross-building. It fixes `clean.sh`, which was a
  no-op, makes both scripts cwd-independent, and adds `.build-lock`
  handling. It also adds CI `test` and `rack-tests` jobs and leaves the CI
  `build` job untouched. It asks before making `publish` depend on them.
- **Plan 007** is investigate-first. It adds a Rack-linked test and stops at
  a decision gate with options A/B/C.
- **[RackSim address migration](racksim-address-migration.md)** is a *note*
  for the RackSim repo, not a plan for this one. RackSim still uses 17 old
  addresses, and it must switch together with ctrl, after plan 001.

## Considered and rejected

Don't re-audit these without new evidence:

- **`ring.clear()` racing the producer in Tx/AudioStreamer.** Rack's
  `dsp::RingBuffer::clear()` only resets the consumer index, and it's called
  from the consumer thread. It's safe.
- **Opus `Packet` size overflow.** Bounded by 255 frames × 0xffff bytes,
  well within its types.
- **UDP send treating `EINTR`/`ECONNREFUSED` as success.** Negligible for
  best-effort datagrams. A retry loop would add latency for no gain.
- **`/patch/open` accepting an arbitrary path.** A documented feature for a
  trusted client.
- **Binding `ANY_ADDRESS` and the unbounded UI action queue.** By design: a
  single trusted client on the LAN. Revisit only if the threat model changes.
- **Splitting `OscReceiver.cpp` into per-domain route files.** Worthwhile
  eventually, but defer until plan 006's characterization tests exist.
- **The audit claim that "EngineClock dereferences the deleted module"**
  (`docs/audit.md`). Incorrect: the pointer is only compared. The real
  question (the detach path under `Engine::clear()`) is plan 007.

## Direction ideas (not planned)

- **Blank / resizable module width.** Expose panel width changes over OSC so
  clients can lay out blanks and resizable modules without re-requesting
  structure.
- **Param-state subscriptions.** Lights already have
  `/subscribe/module/lights`. A matching param subscription would replace
  polling `/get/module/params/state` (the change-only reporting already exists).
- **Cable history.** The TODOs at `src/osc/OscReceiver.cpp:868,908` note that
  OSC-driven cable add/remove isn't recorded in Rack's undo history. Adding
  that would make remote edits undoable in the Rack UI.

## Not audited

Vendored dependencies (`dep/`, Opus, oscpack, QOI), the Rack SDK itself, the
RackSim repo beyond its `Comms/` OSC addresses and `TextureStore.h`, and
macOS/Windows-specific build paths beyond reading CI config.
