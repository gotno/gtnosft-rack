# Note: RackSim OSC address migration

> **This is a note, not an executable plan.** It describes changes needed in
> the **RackSim** repository (`/home/ont/dev/RackSim`, the Unreal client), not
> in gtnosft-rack. It is here because the renames happened in ctrl, and
> RackSim must follow them. Do not edit RackSim from a gtnosft-rack plan run.
>
> Written against: gtnosft-rack `e444a8f` (plus plan 001's fixes), RackSim
> `b491f78`, 2026-11-12. All RackSim paths below are relative to
> `Source/RackSim/Comms/`.

## Summary

ctrl's route rename is finished in ctrl (plan 001 fixes the last three
inconsistencies). RackSim still uses the **old** names for 17 addresses: 5
it sends and 12 it listens for. Until RackSim is updated, those requests are
ignored by ctrl, and those replies are dropped by RackSim. Module structure,
module state, param state, lights and textures are all affected, so RackSim
and ctrl must be switched together.

## Requests RackSim sends (`OSCSender.cpp`)

| Line | Old address | New address | Payload change |
|---:|---|---|---|
| 65 | `/get/module_structure` | `/get/module/structure` | none (plugin slug, module slug) |
| 99 | `/ack_chunk` | `/ack/chunk` | none |
| 112 | `/get/module_state` | `/get/module/state` | none (int64 module id) |
| 123 | `/get/params_state` | `/get/module/params/state` | none (int64 module id) |
| 228 | `/audio/stats` | `/set/audio/stats` | none (int64 id, 3×int32, 2×float) |

## Replies RackSim listens for (`OSCReceiver.cpp`)

| Line | Old address | New address |
|---:|---|---|
| 82 | `/set/module_structure` | `/set/module/structure` |
| 88 | `/set/module_structure/param` | `/set/module/structure/param` |
| 94 | `/set/module_structure/param/knob` | `/set/module/structure/param/knob` |
| 100 | `/set/module_structure/param/slider` | `/set/module/structure/param/slider` |
| 106 | `/set/module_structure/param/button` | `/set/module/structure/param/button` |
| 112 | `/set/module_structure/param/switch` | `/set/module/structure/param/switch` |
| 118 | `/set/module_structure/port` | `/set/module/structure/port` |
| 124 | `/set/module_structure/light` | `/set/module/structure/light` |
| 131 | `/set/texture` | `/set/texture/chunk` |
| 138 | `/set/s/m` | `/set/module/state` |
| 145 | `/set/s/p` | `/set/param/state` |
| 152 | `/set/s/l` | `/set/lights/state` ¹ |

¹ At `e444a8f`, ctrl actually sends `/set/state/lights`, which is the odd one
out. Plan 001 renames it to `/set/lights/state` to match `API.md`. Update
RackSim to `/set/lights/state` and land it with (or after) plan 001.

Payloads are unchanged for all 12. Check them against `API.md` while
migrating anyway.

## Unchanged (no action)

Sent: `/register`, `/keepalive`, `/get/settings`, `/get/modules`,
`/get/cables`, `/get/texture`, `/subscribe/module/lights`,
`/set/param/value`, `/add/cable`, `/remove/cable`, `/set/audio/enabled`,
`/set/audio/preset`.

Received: `/announce`, `/heartbeat`, `/set/settings`, `/set/module/stub`,
`/set/cable`, `/report/module/count`, `/report/cable/count`,
`/ack/param/value`, `/ack/cable/add`, `/ack/cable/remove`,
`/ack/subscribe/module/lights`, `/set/audio_stream`,
`/ack/audio/enabled`, `/ack/audio/preset`.

## Available in ctrl, not yet handled by RackSim (optional)

- **`/report/param/state/count`** (int64 module id, int64 count). It is sent
  after the `/set/param/state` messages for a `/get/module/params/state`
  request, in the same bundle, so it arrives last. Useful as an
  "update complete" marker. Later requests report only changed params.
- **`/act/click/param`, `/act/press/param`, `/act/release/param`** (int64
  module id, int32 param id). These synthesize widget mouse interactions in
  Rack, e.g. for momentary buttons that read `ParamWidget` events rather than
  values. Each is answered by `/ack/click/param`, `/ack/press/param` or
  `/ack/release/param`, with payload (int64 module id, int32 param id,
  bool success). Before plan 001, ctrl answered these with a malformed ACK
  address. Don't rely on them against a ctrl older than plan 001. See
  `API.md` "`/act/click/param`…" for the hold, click and ordering rules.

## Related constraints from other plans

- **Plan 004** caps `/get/texture` arguments: scale must be in (0, 16]. Width
  and height, when given, must be in [1, 8192]. RackSim requests heights of
  `1024 >> level` (1024/512/256/128), plus 496 (quick) and 128 (atlas)
  (`Source/RackSim/TextureStore.h:12-21`), which are well inside these limits. Out-of-range requests are now silently
  ignored (logged on the Rack side), so RackSim should not wait forever on one.
- **Plan 001** adds `tools/check_api_addresses.py`, which checks that ctrl's
  routes match `API.md`. RackSim can be checked against `API.md` the same
  way with a grep over `TEXT("/` and `FString("/` literals.

## Suggested order

1. Land gtnosft-rack plan 001.
2. In RackSim, change the 17 strings above, in one commit.
3. Run RackSim against the new ctrl build. Load a patch, confirm the module
   panels, params, lights and textures populate, and confirm no
   "unhandled address" logs appear on either side.
