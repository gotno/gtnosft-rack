# Plan 001: Finish the OSC route rename so code, API.md and bench tools agree

> **Executor instructions**: Follow this plan step by step. Run every
> verification command and confirm the expected result before moving to the
> next step. If anything in the "STOP conditions" section occurs, stop and
> report. Do not improvise. When done, update the status row for this plan
> in `plans/README.md`, unless a reviewer dispatched you and told you they
> maintain the index.
>
> **Drift check (run first)**:
> ```bash
> git diff --stat e444a8f..HEAD -- src/osc/OscReceiver.cpp \
>   src/osc/Bundler/ParamActionAckBundler.cpp src/osc/Bundler/ModuleLightsBundler.cpp \
>   API.md tools/bench/overlay_bench.py tools/bench/audio_bench.py \
>   tools/bench/mock_server.py tools/bench/README.md tools/check_api_addresses.py
> ```
> If any in-scope file changed since this plan was written, compare the
> "Current state" excerpts against the live code before proceeding. Treat any
> mismatch as a STOP condition.

## Status

- **Priority**: P1
- **Effort**: S
- **Risk**: LOW (wire-visible: clients must use the new names, see `plans/racksim-address-migration.md`)
- **Depends on**: none
- **Category**: bug
- **Planned at**: commit `e444a8f`, 2026-11-12

## Why this matters

Commit `c62c5f5` ("update routes for consistency") renamed most OSC addresses,
but it left several pieces half done:

- The new `/act/<action>/param` routes are registered under the wrong
  address (`/actclick/param`), so click/press/release requests are silently
  ignored.
- The param action ACKs and the lights update use addresses that differ from
  API.md.
- API.md still names two old addresses.
- The benchmark tools in `tools/bench/` still speak the old protocol, so
  `overlay_bench.py`'s texture ACKs and module-state requests go unanswered.

This plan makes the code, API.md and the tools agree. It also adds a small
script that diffs the addresses in `src/` against API.md, so drift like this
is caught automatically (plan 005 wires it into CI).

## Current state

Files:

- `src/osc/OscReceiver.cpp`: OSC route table (`OscReceiver::generateRoutes`).
  Lines 697-706 build the `/act/*/param` routes:
  ```cpp
  for (auto [actionName, action] : {
    // /act/click/param
    std::pair{"click", gtnosft::param_interaction::Action::Click},
    // /act/press/param
    std::pair{"press", gtnosft::param_interaction::Action::Press},
    // /act/release/param
    std::pair{"release", gtnosft::param_interaction::Action::Release}
  }) {
    routes.emplace(
      std::string("/act") + actionName + std::string("/param"),
  ```
  The bug: `"/act" + "click" + "/param"` is `/actclick/param`. It is missing a
  `/`.
- `src/osc/Bundler/ParamActionAckBundler.cpp:10-12`: the ACK for those routes.
  ```cpp
  void ParamActionAckBundler::addMessage(bool success) {
    messages.emplace_back(
      "/ack/param/" + action,
  ```
  This sends `/ack/param/click`. API.md (lines 548-549) documents
  `/ack/click/param`, `/ack/press/param` and `/ack/release/param`.
  **Decision (assumption):** the documented form `/ack/<action>/param` is
  canonical. It mirrors `/act/<action>/param` and matches the noun-last style
  of the rest of the rename (`/set/module/state`, `/set/param/state`).
  Change the code, not the docs.
- `src/osc/Bundler/ModuleLightsBundler.cpp:47-49`:
  ```cpp
  void ModuleLightsBundler::addMessage(const std::vector<LightEntry>& batch) {
    messages.emplace_back(
      "/set/state/lights",
  ```
  But `ModuleLightsBundler.hpp:53` sizes the message with
  `sizeof("/set/lights/state")`, and API.md (lines 129 and 653) documents
  `/set/lights/state`. Change the code to `/set/lights/state`. Both strings
  have the same length, so the size math is unaffected.
- `API.md` (consumer-facing protocol doc; AGENTS.md says to keep internal
  rationale out of it):
  - Lines 434 and 438: `/get/module_state` is the heading and request name.
    The code registers `/get/module/state` (`OscReceiver.cpp:612`).
  - Line 482: `` **`/report/state/param/count`** ``. The code sends
    `/report/param/state/count` (`src/osc/Bundler/ModuleParamsBundler.cpp:46,76`),
    and API.md line 464 already uses the new name.
- `tools/bench/overlay_bench.py`: the Python benchmark client (uses
  `pythonosc`; run with `tools/bench/.venv/bin/python`).
  - Line 96: `_ACK_PREFIX = b"/ack_chunk\0\0,hii\0\0\0\0"`. This is a
    hand-encoded OSC address + type tag, used at line 339 to ACK each texture
    chunk. The new address is `/ack/chunk`. It is also 10 characters, so the
    padding is identical: `b"/ack/chunk\0\0,hii\0\0\0\0"`.
  - Line 299: `elif a == "/set/s/m":` should become `/set/module/state`
    (same payload: module id, x, y, overlay texture id).
  - Line 457: `self.send("/get/module_state", ("h", target["id"]))` should
    become `/get/module/state`.
- `tools/bench/audio_bench.py`: lines 103 (docstring `"""/audio/stats arguments ...`)
  and 155 (`"/audio/stats", ...`). The route is now `/set/audio/stats`.
- `tools/bench/mock_server.py`: a mock ctrl used to test the clients without
  Rack.
  - Line 158 `elif a == "/ack_chunk":` → `/ack/chunk`
  - Line 178 `elif a == "/audio/stats":` → `/set/audio/stats`
  - Line 187 `elif a == "/get/module_state":` → `/get/module/state`
  - Line 189 `send(msg("/set/s/m", ...` → `/set/module/state`
  - Line 227 `"/set/texture", ("h", tid), ...` → `/set/texture/chunk`
    (`overlay_bench.py:330` already expects `/set/texture/chunk`).
- `tools/bench/README.md`: line 224 (`/ack_chunk`), lines 315 and 337
  (`/audio/stats`) describe current behavior. Update them. Line 233's
  `module_state` is an action-queue label (`OscReceiver.cpp:620`), not an
  address. Leave it.

Full rename map from `c62c5f5`, for reference. Most entries are already
correct in `src/`:

| Old | New |
|---|---|
| `/set/texture` | `/set/texture/chunk` |
| `/set/s/l` | `/set/lights/state` |
| `/set/s/m` | `/set/module/state` |
| `/set/s/p` | `/set/param/state` |
| `/report/state/param/count` | `/report/param/state/count` |
| `/set/module_structure*` | `/set/module/structure*` |
| `/get/module_structure` | `/get/module/structure` |
| `/ack_chunk` | `/ack/chunk` |
| `/get/module_state` | `/get/module/state` |
| `/get/params_state` | `/get/module/params/state` |
| `/param/<action>` | `/act/<action>/param` |
| `/ack/param/<action>` | `/ack/<action>/param` |
| `/audio/stats` | `/set/audio/stats` |

Conventions: commit messages are short, lowercase and imperative (e.g.
`send plugin version in /announce`). Python in `tools/` uses the stdlib
where possible and starts with a module docstring (see
`tools/bench/audio_analysis.py`).

## Commands you will need

| Purpose | Command | Expected on success |
|---|---|---|
| Build plugin (WSL/Linux) | `./build-wsl.sh -j8` | exit 0. Never run two build wrappers at once (shared `.build-lock`) |
| Rack-free tests | `./tests/run.sh` | all tests pass, exit 0 |
| Address check (new) | `python3 tools/check_api_addresses.py` | `OK: 70 addresses match`, exit 0 |
| Bench unit tests | `cd tools/bench && python3 -m unittest -v test_audio_analysis` | `OK` |

## Scope

**In scope** (the only files you should modify or create):
- `src/osc/OscReceiver.cpp` (only the `/act` route string, line ~706)
- `src/osc/Bundler/ParamActionAckBundler.cpp`
- `src/osc/Bundler/ModuleLightsBundler.cpp`
- `API.md` (only lines 434, 438, 482)
- `tools/bench/overlay_bench.py`, `tools/bench/audio_bench.py`,
  `tools/bench/mock_server.py`, `tools/bench/README.md`
- `tools/check_api_addresses.py` (create)

**Out of scope** (do NOT touch):
- `tools/bench/PERFLOG.md`: a historical log. Old names there were correct
  for their time.
- `docs/audit.md`: historical.
- RackSim (the Unreal client, separate repo). Its migration is described in
  `plans/racksim-address-migration.md` and is done separately.
- Any payload or argument change. Only address strings change.
- `.github/workflows/`: plan 005 adds the CI step.

## Git workflow

- Work on the current branch, or `advisor/001-finish-route-rename` if the
  operator asks for a branch.
- **Do not commit.** The maintainer reviews and commits. Leave changes in the
  working tree.

## Steps

### Step 1: Add the address-check script

Create `tools/check_api_addresses.py` with exactly this content:

```python
#!/usr/bin/env python3
"""Check that OSC addresses in src/ and API.md match.

Collects quoted "/..." string literals from src/ (line comments stripped) and
backticked `/...` addresses from API.md, then reports any address found in
only one of them. Exits 1 on a mismatch.

Usage (from the repository root): python3 tools/check_api_addresses.py
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# literals that are only pieces of addresses assembled at runtime
# (e.g. "/act/" + action + "/param", bench toggles' address + "/ack")
FRAGMENTS = {"/act/", "/ack/", "/param", "/ack"}
# the addresses those pieces produce
ACTIONS = ("click", "press", "release")
ASSEMBLED = (
    {f"/act/{a}/param" for a in ACTIONS}
    | {f"/ack/{a}/param" for a in ACTIONS}
)

ADDRESS = r"/[a-z0-9_]+(?:/[a-z0-9_*]+)*/?"


def code_addresses():
    found = set()
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in (".cpp", ".hpp"):
            continue
        text = re.sub(r"//[^\n]*", "", path.read_text())
        for match in re.finditer(rf'"({ADDRESS})"', text):
            found.add(match.group(1))
    return (found - FRAGMENTS) | ASSEMBLED


def doc_addresses():
    text = (ROOT / "API.md").read_text()
    found = {m.group(1) for m in re.finditer(rf"`({ADDRESS})`", text)}
    return found - FRAGMENTS


def main():
    code, doc = code_addresses(), doc_addresses()
    # API.md documents some address families with a wildcard
    wildcards = {d for d in doc if "*" in d}
    doc -= wildcards
    for wildcard in wildcards:
        pattern = re.compile(
            re.escape(wildcard).replace(r"\*", "[a-z0-9_]+") + "$"
        )
        code = {c for c in code if not pattern.match(c)}

    only_code, only_doc = sorted(code - doc), sorted(doc - code)
    for address in only_code:
        print(f"in src/ but not API.md: {address}")
    for address in only_doc:
        print(f"in API.md but not src/: {address}")
    if only_code or only_doc:
        return 1
    print(f"OK: {len(code | doc)} addresses match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

**Verify**: `python3 tools/check_api_addresses.py; echo "exit=$?"` prints
exactly these 6 lines, then `exit=1`:
```
in src/ but not API.md: /ack/param/
in src/ but not API.md: /act
in src/ but not API.md: /get/module/state
in src/ but not API.md: /set/state/lights
in API.md but not src/: /get/module_state
in API.md but not src/: /report/state/param/count
```
(The advisor confirmed this logic against `e444a8f`. If the output differs,
STOP.)

### Step 2: Fix the `/act/<action>/param` route address

In `src/osc/OscReceiver.cpp` (line ~706), change
`std::string("/act") + actionName + std::string("/param")` to
`std::string("/act/") + actionName + std::string("/param")`.

**Verify**: `grep -n 'std::string("/act/") + actionName' src/osc/OscReceiver.cpp`
→ exactly one match.

### Step 3: Fix the param action ACK and lights addresses

- In `src/osc/Bundler/ParamActionAckBundler.cpp`, change
  `"/ack/param/" + action` to `"/ack/" + action + "/param"`.
- In `src/osc/Bundler/ModuleLightsBundler.cpp`, change `"/set/state/lights"`
  to `"/set/lights/state"`.

**Verify**: `grep -rn '/ack/param/"\|/set/state/lights' src/` → no matches.

### Step 4: Fix the two stale names in API.md

- Lines 434 and 438: replace `/get/module_state` with `/get/module/state`
  (the `####` heading and the `**Request:**` line).
- Line 482: replace `/report/state/param/count` with
  `/report/param/state/count`.
- Do not change anything else in API.md.

**Verify**: `grep -n "module_state\|state/param/count" API.md` → no matches;
`python3 tools/check_api_addresses.py` → `OK: 70 addresses match`, exit 0.

### Step 5: Update the bench tools and their README

Apply the edits listed under "Current state" for `overlay_bench.py` (lines 96,
299, 457), `audio_bench.py` (103, 155), `mock_server.py` (158, 178, 187, 189,
227) and `README.md` (224, 315, 337). Make `_ACK_PREFIX` exactly
`b"/ack/chunk\0\0,hii\0\0\0\0"`. OSC strings are null-terminated and padded to
a multiple of 4 bytes, and `/ack/chunk` (10 chars) pads to 12 bytes like the
old address did.

**Verify** (both commands print nothing):
```bash
grep -rn 'ack_chunk\|/set/s/\|"/set/texture"\|"/get/module_state"\|[^t]/audio/stats' tools/bench/*.py
grep -n 'ack_chunk\|[^t]/audio/stats' tools/bench/README.md
```
(`[^t]/audio/stats` matches the old `/audio/stats` but not `/set/audio/stats`.)

### Step 6: Smoke-test the bench client against the mock server

If `tools/bench/.venv` does not exist, skip this step and say so in your
report. Do not install packages. Otherwise, from `tools/bench/`:
```bash
cd tools/bench
.venv/bin/python mock_server.py --port 17225 &
MOCK=$!
sleep 1
.venv/bin/python overlay_bench.py run --host 127.0.0.1 --port 17225 --duration 1 --idle 0.3 --warmup 0
STATUS=$?
kill $MOCK
echo "exit=$STATUS"
```
**Verify**: `exit=0`, and the report shows completed textures (not only
timeouts or failures). Then
`python3 -m unittest -v test_audio_analysis` → `OK`.

### Step 7: Build and run the tests

**Verify**: `./build-wsl.sh -j8` → exit 0; `./tests/run.sh` → all pass;
`python3 tools/check_api_addresses.py` → `OK: 70 addresses match`.

## Test plan

- The address-check script is the regression test for this class of bug. It
  fails on the current tree (Step 1) and passes after the fix (Step 4).
- No new C++ tests. The bugs are string literals that the script covers.
- The mock smoke test (Step 6) exercises the Python tools end to end.

## Done criteria

- [ ] `python3 tools/check_api_addresses.py` prints `OK: 70 addresses match` and exits 0
- [ ] `grep -rn '"/act")\|/ack/param/"\|/set/state/lights' src/` → no matches
- [ ] Both Step 5 grep commands print nothing
- [ ] `./build-wsl.sh -j8` exits 0 and `./tests/run.sh` passes
- [ ] `git status` shows only in-scope files modified or created
- [ ] `plans/README.md` status row updated

## STOP conditions

Stop and report back (do not improvise) if:
- Step 1's output differs from the 6 expected lines. The tree has drifted,
  and the expected count of 70 is no longer valid.
- After Step 4 the script still reports mismatches you can't trace to a line
  this plan lists.
- The maintainer says `/ack/param/<action>` (not `/ack/<action>/param`) is the
  intended ACK form. In that case, edit API.md lines 548-549 instead of the
  bundler, and change the script's `ASSEMBLED` to match.
- The mock smoke test fails for a reason other than an address mismatch.
  Report the output.

## Maintenance notes

- Any new OSC address must appear in both `src/` (as a string literal) and
  API.md (in backticks), or the check script fails. If you build an address
  dynamically, add its pieces to `FRAGMENTS` and its expansions to
  `ASSEMBLED`.
- The script strips only `//` comments. A quoted address inside a `/* */`
  block would still be counted.
- This is a wire change for clients. RackSim must be migrated at the same time
  (see `plans/racksim-address-migration.md`).
