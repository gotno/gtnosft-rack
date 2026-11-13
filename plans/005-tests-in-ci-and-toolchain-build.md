# Plan 005: Run the test suites in CI and gate the toolchain release build on them

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report. Do not improvise. When done, update the status row for this plan
> in `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**, from the gtnosft-rack root:
> `git diff --stat e444a8f..HEAD -- .github/workflows/ tests/run.sh tests/run-rack.sh Makefile build-wsl.sh build-common.sh`
> and `cat -A ../rack-plugin-toolchain/build.sh ../rack-plugin-toolchain/clean.sh`
> (both must match "Current state" below, line for line). If an in-scope file
> changed since this plan was written, compare the excerpts against the live
> files before proceeding. Treat any mismatch as a STOP condition.

## Status

- **Priority**: P2
- **Effort**: M
- **Risk**: LOW (CI and build scripts only; no plugin code changes)
- **Depends on**: plans/001-finish-route-rename.md (it creates `tools/check_api_addresses.py`, which both CI and `build.sh` run)
- **Category**: dx / tests
- **Planned at**: commit `e444a8f`, 2026-11-12 (revised the same day: the maintainer builds releases with the local rack-plugin-toolchain scripts, so this plan doesn't pin a Docker image)

## Why this matters

Nothing runs the test suites automatically. GitHub CI only builds the plugin.
The maintainer's local release build (`../rack-plugin-toolchain/build.sh`)
only cross-compiles. The Rack-free C++ suite (`tests/run.sh`, 45 tests at
planning time), the headless Rack-linked tests (`tests/run-rack.sh`), the
bench analysis unit tests and (after plan 001) the OSC address check are all
run by hand, or not at all. So regressions in packet framing, pacing, param
writes or the protocol docs can reach `main` and a release unnoticed.

The local toolchain scripts also have three defects:

1. `clean.sh` does nothing beyond deleting `.d` files. Its `make clean`
   targets the *toolchain's* Makefile, which has no `clean` target. There's
   also a stray file named `clean` in the toolchain dir, a copy of
   `clean.sh`, so make just prints `make: 'clean' is up to date.`. The real
   target is `plugin-build-clean`.
2. Both scripts use relative paths (`../gtnosft-rack`) and only work when run
   from the toolchain directory.
3. `build.sh` doesn't respect gtnosft-rack's `.build-lock`. The toolchain
   target runs `make clean` in gtnosft-rack (`rm -rf build plugin.so dist`),
   which corrupts a concurrently running `./build-wsl.sh`.

## Current state

### `../rack-plugin-toolchain/build.sh` (untracked local file, 4 lines)

```bash
#!/bin/bash

find ../gtnosft-rack/build -type f -name '*.d' -delete
make -j4 plugin-build PLUGIN_DIR=../gtnosft-rack
```

### `../rack-plugin-toolchain/clean.sh` (untracked local file, 4 lines)

```bash
#!/bin/bash

find ../gtnosft-rack/build -type f -name '*.d' -delete
make clean
```

`../rack-plugin-toolchain/clean` (no extension) is byte-identical to
`clean.sh` (`cmp clean clean.sh` reports no difference). Both scripts and
`clean` are untracked in the toolchain repo, whose origin is
`VCVRack/rack-plugin-toolchain`. **Never edit any tracked toolchain file**
(e.g. its `Makefile`).

The `.d` deletion exists because stale dependency files from a WSL or MSYS
build in `build/` reference paths the cross-compiler can't resolve. The
plugin Makefile includes them even for `make clean`. Keep it.

### What `make plugin-build PLUGIN_DIR=...` does (toolchain `Makefile:204-277`)

It builds mac-x64, mac-arm64, win-x64 and lin-x64 in turn. For each one, it
runs this inside the plugin dir:
```make
	cd $(PLUGIN_DIR) && $(MAKE) clean        # rm -rf build plugin.* dist
	cd $(PLUGIN_DIR) && $(MAKE) cleandep     # rm -rf $(DEP_LOCAL) = dep/
	cd $(PLUGIN_DIR) && $(MAKE) dep
	cd $(PLUGIN_DIR) && $(MAKE) dist
	mkdir -p $(PLUGIN_BUILD_DIR)
	cp $(PLUGIN_DIR)/dist/*.vcvplugin $(PLUGIN_BUILD_DIR)/
	cd $(PLUGIN_DIR) && $(MAKE) clean
```
`PLUGIN_BUILD_DIR` is `plugin-build/` in the toolchain dir. The toolchain
uses Rack SDK `2.6.6` (`RACK_SDK_VERSION`). The toolchain dir has
`Rack-SDK-lin-x64/libRack.so`. It **deletes `plugin.so` at the end**, so the
Rack-linked tests must run *before* it, against a native build.
`plugin-build-clean` is `rm -rf plugin-build`.

### gtnosft-rack build and test entry points

- `./build-wsl.sh [make args]`: native Linux build. It restores `build/` from
  `build-wsl/`, runs `make` with `DEP_LOCAL=dep-wsl`, and saves the cache
  back. It creates `.build-lock` and **exits 1 if `.build-lock` already
  exists**, removing it on exit. It uses `$RACK_DIR` (the maintainer's is
  `/home/ont/dev/rack/rack-git`; the Makefile default `../..` is wrong for
  this checkout).
- `./build-wsl.sh dep` builds libopus into `dep-wsl/` if it's missing.
- `./tests/run.sh`: needs `dep-wsl/lib/libopus.a`. The last line is
  `N tests, 0 failed checks`, with exit 0.
- `./tests/run-rack.sh`: needs `plugin.so` in the repo root and
  `$RACK_DIR/libRack.so`.
- `tools/check_api_addresses.py` (plan 001): prints `OK: N addresses match`.
- `cd tools/bench && python3 -m unittest -v test_audio_analysis`: stdlib only.

### `.github/workflows/build-plugin.yml`

`on: [push, pull_request]`, with env `rack-sdk-version: 2.6.4`. Jobs:
`modify-plugin-version`, `build` (container
`ghcr.io/qno/rack-plugin-toolchain-win-linux`, matrix win-x64 and lin-x64),
`build-mac`, and `publish` (`needs:  [build, build-mac]`, on `v*` tags). The
`build` job and its container are **out of scope**. Leave them unchanged.
Libopus flags (`Makefile:66-90`, `OPUS_CMAKE_FLAGS`):
```make
OPUS_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release \
	-DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
	-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF \
	-DOPUS_DRED=OFF -DOPUS_OSCE=OFF -DOPUS_FIXED_POINT=OFF \
	-DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON
```

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Validate workflow YAML | `python3 -c "import yaml; yaml.safe_load(open('.github/workflows/build-plugin.yml'))"` | exit 0 (skip if PyYAML is missing) |
| Lint shell scripts (if available) | `shellcheck ../rack-plugin-toolchain/build.sh ../rack-plugin-toolchain/clean.sh` | no output |
| Release build + test gate | `../rack-plugin-toolchain/build.sh` | four `.vcvplugin` files in `../rack-plugin-toolchain/plugin-build/` |

## Scope

**In scope**:
- `.github/workflows/build-plugin.yml` (add two jobs only)
- `../rack-plugin-toolchain/build.sh`, `../rack-plugin-toolchain/clean.sh`
  (untracked; rewrite)
- `../rack-plugin-toolchain/clean` (delete it; it's a stray copy of `clean.sh`)

**Out of scope**:
- Every tracked file in `../rack-plugin-toolchain/` (Makefile, Dockerfile, …).
- `tests/run.sh`, `tests/run-rack.sh`, `build-wsl.sh`, `build-common.sh` and
  the gtnosft-rack `Makefile`. If they can't be used as-is, STOP.
- The CI `build` job (its container image) and the `build-mac` job.
  `publish.needs` changes only with operator approval (Step 5).

## Git workflow

- gtnosft-rack: current branch, or `advisor/005-tests-gate` if asked.
- **Do not commit or push** in either repo. The maintainer reviews and commits.

## Steps

### Step 1: Rewrite `../rack-plugin-toolchain/build.sh`

Replace the whole file with:

```bash
#!/bin/bash
# Test gtnosft-rack natively, then cross-build release packages for all
# platforms into plugin-build/. Run from anywhere.
# Usage: ./build.sh [--skip-tests]
set -euo pipefail

TOOLCHAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$TOOLCHAIN_DIR/../gtnosft-rack" && pwd)"
LOCK_FILE="$PLUGIN_DIR/.build-lock"

skip_tests=0
if [[ "${1:-}" == "--skip-tests" ]]; then skip_tests=1; fi

if [[ $skip_tests -eq 0 ]]; then
  : "${RACK_DIR:?set RACK_DIR to the Rack SDK or source tree used by build-wsl.sh}"
  cd "$PLUGIN_DIR"
  ./build-wsl.sh dep
  ./build-wsl.sh -j4
  ./tests/run.sh
  ./tests/run-rack.sh
  python3 tools/check_api_addresses.py
  (cd tools/bench && python3 -m unittest -q test_audio_analysis)
fi

if [[ -f "$LOCK_FILE" ]]; then
  echo "build.sh: $LOCK_FILE exists; another build may be running" >&2
  exit 1
fi
touch "$LOCK_FILE"
trap 'rm -f "$LOCK_FILE"' EXIT INT TERM

find "$PLUGIN_DIR/build" -type f -name '*.d' -delete 2>/dev/null || true
cd "$TOOLCHAIN_DIR"
make -j4 plugin-build PLUGIN_DIR="$PLUGIN_DIR"
```

Notes for the executor:
- The test gate runs **before** the lock is taken. `build-wsl.sh` takes and
  releases `.build-lock` itself, and it would refuse to run if `build.sh`
  already held it.
- Use `./build-wsl.sh`, not plain `make`, for the native build. It keeps the
  WSL build cache and `dep-wsl/` separate from the toolchain's `dep/`.
- If plan 001 hasn't landed (`tools/check_api_addresses.py` is missing),
  omit that line and say so in the report.
- Keep `chmod +x` (`ls -l` should show `x`).

**Verify**: `bash -n ../rack-plugin-toolchain/build.sh` → exit 0; `shellcheck`
(if installed) → no output.

### Step 2: Rewrite `../rack-plugin-toolchain/clean.sh` and delete `clean`

Replace `clean.sh` with:

```bash
#!/bin/bash
# Remove cross-build outputs: the plugin's build tree, stale .d files and
# the toolchain's plugin-build/ packages. Leaves build-wsl/ and dep-wsl/ alone.
set -euo pipefail

TOOLCHAIN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_DIR="$(cd "$TOOLCHAIN_DIR/../gtnosft-rack" && pwd)"

if [[ -f "$PLUGIN_DIR/.build-lock" ]]; then
  echo "clean.sh: $PLUGIN_DIR/.build-lock exists; a build may be running" >&2
  exit 1
fi

find "$PLUGIN_DIR/build" -type f -name '*.d' -delete 2>/dev/null || true
cd "$TOOLCHAIN_DIR"
make plugin-build-clean
```

Then `rm ../rack-plugin-toolchain/clean`. It's an untracked duplicate, and
its name shadowed make's `clean` goal.

**Verify**:
- `bash -n ../rack-plugin-toolchain/clean.sh` → exit 0
- `(cd /tmp && /home/ont/dev/rack/rack-plugin-toolchain/clean.sh)` → prints
  `rm -rf plugin-build` and exits 0. It must work from another cwd.
- `ls ../rack-plugin-toolchain/plugin-build` → `No such file or directory`
- `git -C ../rack-plugin-toolchain status --short` → only `?? build.sh`
  and `?? clean.sh`. No tracked files are modified.

### Step 3: Run the release build end to end

From the gtnosft-rack root, with `RACK_DIR` exported (the maintainer's value
is `/home/ont/dev/rack/rack-git`):
```bash
../rack-plugin-toolchain/build.sh 2>&1 | tail -40
```
This takes a while (one native build plus four cross builds). Don't run any
other build wrapper at the same time.

**Verify**:
- The output contains `N tests, 0 failed checks` twice (the Rack-free and
  Rack-linked suites), `OK: ... addresses match`, and `OK` from unittest.
- `ls ../rack-plugin-toolchain/plugin-build/` → four files,
  `gotnosoft-<version>-{lin-x64,win-x64,mac-x64,mac-arm64}.vcvplugin`, with
  current mtimes.
- `ls .build-lock` → `No such file or directory` (lock released).
- `git status --short` in gtnosft-rack → nothing new except `plans/`.
  `build/`, `dep/` and `dist/` are gitignored.

Then check that the gate actually gates (local only, revert right after):
temporarily make one `CHECK` fail in any `tests/test_*.cpp`, run
`../rack-plugin-toolchain/build.sh`, and confirm it exits non-zero *before*
any `plugin-build` output. Revert the test, and confirm with
`git diff --stat -- tests/` → empty.

### Step 4: Add CI test jobs

The CI `build` job is unchanged. Add these two jobs under `jobs:`, after
`modify-plugin-version`:

```yaml
  test:
    name: tests
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build libopus
        run: |
          cmake -S dependencies/opus -B dep-ci/opus-build \
            -DCMAKE_BUILD_TYPE=Release \
            -DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
            -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF \
            -DOPUS_DRED=OFF -DOPUS_OSCE=OFF -DOPUS_FIXED_POINT=OFF \
            -DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DCMAKE_INSTALL_PREFIX="$PWD/dep-ci" -DCMAKE_INSTALL_LIBDIR=lib
          cmake --build dep-ci/opus-build -j"$(nproc)"
          cmake --install dep-ci/opus-build
      - name: Rack-free tests
        run: DEP_LOCAL=dep-ci ./tests/run.sh
      - name: OSC address check
        run: python3 tools/check_api_addresses.py
      - name: Bench analysis tests
        working-directory: tools/bench
        run: python3 -m unittest -v test_audio_analysis

  rack-tests:
    name: rack tests
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Get Rack-SDK
        run: |
          pushd $HOME
          wget -O Rack-SDK.zip https://vcvrack.com/downloads/Rack-SDK-${{ env.rack-sdk-version }}-lin-x64.zip
          unzip -q Rack-SDK.zip
      - name: Build plugin
        run: |
          export RACK_DIR=$HOME/Rack-SDK
          make dep
          make -j"$(nproc)"
      - name: Rack-linked tests
        run: RACK_DIR=$HOME/Rack-SDK ./tests/run-rack.sh
```

CI uses plain `make` because the runner is a fresh checkout. `build-wsl.sh`
only exists to share one checkout between WSL and MSYS. The cmake flags are
copied from `OPUS_CMAKE_FLAGS`, so keep them in sync.

**Verify**:
- The YAML validation command → exit 0.
- Locally, prove the `test` job's commands work: run its libopus and test
  commands from the repo root, then `rm -rf dep-ci`. Expect
  `0 failed checks`, `OK: ...` and unittest `OK`. Afterwards,
  `git status --short` shows no `dep-ci`.
- `rack-tests` can only be proven in CI, and `build.sh` (Step 3) already
  covers the same suite locally. If the first CI run fails with
  `error while loading shared libraries: <lib>`, add one
  `sudo apt-get install -y <package>` step before "Rack-linked tests" and
  retry, at most twice.

### Step 5: Gate publishing on tests (ask first)

Do **not** change `publish.needs` unless the operator confirms. The
suggestion is `needs: [build, build-mac, test, rack-tests]`. Report whether
it was applied.

## Test plan

The deliverable is a test gate. Steps 3 and 4 prove it: a full run passes,
and a deliberately broken `CHECK` stops `build.sh` before cross-building. No
new test files.

## Done criteria

- [ ] `build.sh` and `clean.sh` are rewritten, executable, and pass `bash -n`; `../rack-plugin-toolchain/clean` is gone
- [ ] `git -C ../rack-plugin-toolchain status --short` shows only the two untracked scripts, and no tracked toolchain file is modified
- [ ] `../rack-plugin-toolchain/build.sh` runs all four test runners, then produces four `.vcvplugin` files, and leaves no `.build-lock`
- [ ] A failing `CHECK` makes `build.sh` exit non-zero before cross-building (reverted afterwards)
- [ ] `clean.sh` works from another cwd and removes `plugin-build/`
- [ ] The workflow has `test` and `rack-tests` jobs, the YAML validates, and the `build` job is unchanged
- [ ] The report states whether `publish.needs` was changed
- [ ] `plans/README.md` status row updated

## STOP conditions

- The toolchain's `Makefile` `plugin-build-*` recipe differs from the excerpt
  above, for example it no longer ends with `make clean`, or the target
  names changed.
- `./build-wsl.sh` fails while `../rack-plugin-toolchain/build.sh` is not
  running. That's a pre-existing build problem, not this plan's.
- `tests/run.sh` or `tests/run-rack.sh` need changes to work. Report the
  change you'd make instead of making it.
- The cross build fails in a way that also happens with the *original*
  `build.sh`. That's pre-existing. Report it.
- `rack-tests` in CI still fails after two rounds of adding packages.
  Remove that job, keep `test`, and report the log.

## Maintenance notes

- Version skew: the toolchain builds releases against Rack SDK **2.6.6**,
  while the CI workflow uses **2.6.4**, and the native test build uses
  whatever `$RACK_DIR` is. When bumping the SDK, update the toolchain
  `RACK_SDK_VERSION` and the workflow `rack-sdk-version` together.
- If `OPUS_CMAKE_FLAGS` changes, update the CI `test` job's flags too.
- `build.sh --skip-tests` exists for iterating on packaging only. Don't use
  it for a release.
- The toolchain's `plugin-build` wipes gtnosft-rack's `build/`, `plugin.so`,
  `dist/` and `dep/`. That's harmless, because `build-wsl.sh` restores
  `build/` from `build-wsl/` and uses `dep-wsl/`. But never run it alongside
  `build-wsl.sh` or `build-msys.sh`. The lock in `build.sh` enforces this.
