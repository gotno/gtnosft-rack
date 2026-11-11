# ctrl API Documentation

## Overview

ctrl provides a bidirectional [OSC (Open Sound Control)](https://ccrma.stanford.edu/groups/osc/index.html) network API for controlling and monitoring VCV Rack, enabling remote control, state synchronization, and visual rendering of modules and their components.

The server wraps outgoing OSC messages in immediate bundles. A response containing multiple messages may span multiple UDP packets.

## Connection Flow
### 1. Discovery Phase (Broadcast Mode)

ctrl starts in **broadcast mode**, periodically sending `/announce` on the local
network. The OSC receiver remains active in this mode; clients should use
`/register` to establish a directed connection.

#### Announcement

**Broadcast:** `/announce` to UDP port 7746 once per second

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | listen port | the port on which the server is listening for incoming messages (normally 7225; may increment if the port is busy) |
| 1 | int32 | interval | the interval between heartbeat messages in milliseconds |
| 2 | int64 | ctrl id | module id of ctrl module |
| 3 | string | filename | current patch filename (empty string for never-saved patch) |


**Your client should:**
1. Listen for `/announce` broadcasts on UDP port 7746.
2. Extract the listen port and interval, and record the sender's IP address.
3. Register for directed communication (continue to step 2)

### 2. Registration Phase

Once you've discovered the server, register to switch to **direct unicast mode**.

**Request:** `/register`
* (no arguments)

The server switches to direct unicast mode and sends outgoing OSC packets to
the registering client's IP address on UDP port 7746. It continues listening
on the port announced in `/announce`; a direct `/heartbeat` is sent shortly
after registration.

While in broadcast mode, ctrl ignores client requests (`/get/*`, `/set/*`,
`/subscribe/*`, `/add/cable`, `/remove/cable`, `/audio/stats` and
`/patch/open`). Register before sending them. Requests still queued when the
client times out or re-registers are dropped without a reply.

### 3. Heartbeat Maintenance

After registration, maintain the connection by sending periodic heartbeats.
Once ctrl receives the first `/keepalive`, it starts monitoring them. If it
misses 5 consecutive heartbeat intervals, it switches back to broadcast mode
and cancels subscriptions.

**Request:** `/keepalive`
* (no arguments)
* Frequency: the interval received in `/announce` (1000 ms; more frequently is fine)

**The server will:**
- Count consecutive intervals with no heartbeat received.
- Send `/heartbeat` with CPU stats once per interval while in direct mode.

**Response:** `/heartbeat`

| index | type | contents | description |
|---:|---|---|---|
| 0 | float | CPU avg | average CPU load percentage, 0-100 |
| 1 | float | CPU max | maximum CPU load percentage, 0-100 |

---

## Typical Usage Flow

### Initial Setup

```
1. Listen for /announce broadcasts on port 7746
   → Extract RX port (usually 7225)

2. Send /register to RX port
   → Server switches to direct mode

3. Start heartbeat loop: send /keepalive every 1000ms (or more often)
   → Maintains connection

4. Send /get/patch_info
   ← Receive /set/patch_info with ctrlId and filename

5. Send /get/module_stubs
   ← Receive /set/module_stub for each module, then /report/module/count
   → Build list of moduleId → (pluginSlug, moduleSlug)
```

### Getting Module UI Definitions

```
For each unique (pluginSlug, moduleSlug) pair:

1. Send /get/module_structure <pluginSlug> <moduleSlug>
   ← Receive /set/module_structure (header)
   ← Receive /set/module_structure/param/* for each parameter
   ← Receive /set/module_structure/port for each port
   ← Receive /set/module_structure/light for each light

2. Send /get/texture <panelTextureId> <sequenceId> <scale|height>
   (and the same for param and port texture ids)
   ← Receive chunks via /set/texture
   → ACK every copy of each chunk with /ack_chunk
   → Assemble and decode the QOI image

3. Cache this information - only needs to be done once per module type
```

### Getting Module State

```
For each module instance:

1. Send /get/module_state <moduleId>
   ← Receive /set/state/module with position and overlay texture id

2. Send /get/texture <overlayTextureId> <sequenceId> <scale|height>
   ← Receive chunks of the module's current overlay (displays, etc.)

3. Optional: Send /subscribe/module/lights <moduleId>
   ← Receive /ack/subscribe/module/lights, then /set/state/lights updates each UI frame
```

### Controlling Parameters

```
1. Send /set/param/value <moduleId> <paramId> <value> <needsAck>
   ← If needsAck is true, receive /ack/param/value with applied value/status

2. Optional: Request a new overlay render to see any changes related to the param change
   Send /get/texture <overlayTextureId> <sequenceId> <scale|height>
```

### Managing Cables

```
To add a cable:
1. Send /add/cable <inputModuleId> <outputModuleId> <inputPortId> <outputPortId> "#ff0000" <returnId>
   ← Receive /ack/cable/add with returnId, cableId, and success

To get all cables:
1. Send /get/cables
   ← Receive /set/cable for each cable, then /report/cable/count

To remove a cable:
1. Send /remove/cable <cableId>
   ← Receive /ack/cable/remove with cableId and success
```
---

## API Reference

### General Notes
1. All size and position values are in centimeters.
2. Structure positions are relative to the module's top-left corner; module state positions (the module's position in the rack) are relative to ctrl's top-left corner.
3. Positive x values move rightward; positive y values move downward.

### Connection Management

#### `/register`

| Direction | Arguments |
|---|---|
| Client → Server | None |

In broadcast mode, registers the client's IP address and switches outgoing
traffic to direct unicast. A `/register` received while already in direct mode
does not change the connection.

#### `/reset`

| Direction | Arguments |
|---|---|
| Client → Server | None |

Disconnects the registered client, exactly as a heartbeat timeout does: ctrl
drops queued and in-flight sends, subscriptions and cached state, disables the
managed audio stream and switches back to broadcast mode. No reply is sent;
send `/register` to reconnect. Ignored while in broadcast mode.

#### `/keepalive`

| Direction | Arguments |
|---|---|
| Client → Server | None |

Starts connection monitoring on the first heartbeat. Continue sending at the
announced interval; after 5 missed intervals ctrl returns to broadcast mode and
cancels subscriptions.

---
### Rack Settings

#### `/get/settings`

Returns Rack's current display and cable settings.

**Request:** `/get/settings` (no arguments)

**Response:** `/set/settings`

| index | type | contents | description |
|---:|---|---|---|
| 0 | float | rack brightness | current Rack "Room brightness" setting |
| 1 | float | cable opacity | current cable opacity setting |
| 2 | float | cable tension | current cable tension setting |
| 3… | string | cable color | Hex color for each configured cable color, in order |

### Patch Management

#### Patch info `/get/patch_info`
Request current patch information. The ctrl module id can be useful for disambiguating never-saved patches or patches that share a filename.

**Request:** `/get/patch_info` (no arguments)

**Response:** `/set/patch_info`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | ctrl id | module id of ctrl module |
| 1 | string | filename | current patch filename (empty string for never-saved patch) |


---
#### Open a patch `/patch/open`
Loads a patch from the server's local filesystem.

**Request:** `/patch/open`

| index | type | contents | description |
|---:|---|---|---|
| 0 | string | path | filesystem path to the patch file on the server |


**Response:** None. The connection is immediately dropped when the new patch loads. There is no success or failure acknowledgement.

---

### Module Discovery
#### Get loaded modules `/get/module_stubs`
Get the id, plugin slug, and module slug for each module in the patch.

**Request:** `/get/module_stubs` (no arguments)

**Response:** One `/set/module_stub` per loaded module, followed by
`/report/module/count`.

Stubs and their count report are sent as a single bundle to ensure count arrives last.

`/set/module_stub`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | rack's unique module id for the module instance |
| 1 | string | plugin slug | rack's unique library slug for the module's plugin, eg 'gotnosoft' |
| 2 | string | module slug | rack's unique library slug for the module, eg 'Ctrl' |

**`/report/module/count`**

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module count | number of loaded modules |
---


### Structure
Structure represents the general blueprint for a module and its params, ports and lights. It is distinct from State, which is the data unique to a specific instance of a module in the patch.

**Request:** `/get/module_structure`

| index | type | contents | description |
|---:|---|---|---|
| 0 | string | plugin slug | rack's unique library slug for the module's plugin, eg 'gotnosoft' |
| 1 | string | module slug | rack's unique library slug for the module, eg 'Ctrl' |


**Response:** Multiple messages describing the module and its parameters, ports,
and lights. There is no response if the module model cannot be found or loaded.

**Module header:** `/set/module_structure` General module information.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for this module structure definition, also sent with payloads for the module's params, ports, and lights |
| 1 | string | plugin slug | rack's unique library slug for the module's plugin, eg 'gotnosoft' |
| 2 | string | module slug | rack's unique library slug for the module, eg 'Ctrl' |
| 3 | float | size x | width of the module |
| 4 | float | size y | height of the module |
| 5 | int64 | panel texture id | unique id for requesting a render of this module's panel |
| 6 | int32 | num params | number of reported parameters (recognized knobs, sliders, buttons, and switches) |
| 7 | int32 | num inputs | total number of input ports for this module |
| 8 | int32 | num outputs | total number of output ports for this module |
| 9 | int32 | num lights | total number of lights for this module |

---
**Parameter definitions:** Two messages per parameter: One general info and one type-specific info. Positions are relative to module position.

**Parameter header:** `/set/module_structure/param`: Base parameter information
Shared data shape for all param types.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter index for this module |
| 2 | int32 | param type | 1 = knob, 2 = slider, 3 = button, 4 = switch |
| 3 | string | name | parameter name |
| 4 | string | description | parameter description |
| 5 | float | size x | width of the parameter's bounding box |
| 6 | float | size y | height of the parameter's bounding box |
| 7 | float | pos x | x position, relative to module position |
| 8 | float | pos y | y position, relative to module position |
| 9 | float | default value | parameter's default value |
| 10 | float | min value | parameter's minimum value |
| 11 | float | max value | parameter's maximum value |
| 12 | bool | visible | parameter's default visibility (true = visible) |
| 13 | bool | snap | whether the parameter snaps to integer values |


`/set/module_structure/param/knob`: Knob-specific parameter information

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | float | min angle | minimum rotation angle in radians<sup>1</sup> |
| 3 | float | max angle | maximum rotation angle in radians<sup>1</sup> |
| 4 | int64 | background texture id | unique id for requesting a render of the knob's background<sup>2 3</sup> |
| 5 | int64 | midground texture id | unique id for requesting a render of the knob's midground<sup>2 3</sup> |
| 6 | int64 | foreground texture id | unique id for requesting a render of the knob's foreground<sup>2 3</sup> |


<sub>notes:</sub><br />
<sub>1. zero is at 12 o'clock, positive is clockwise</sub><br />
<sub>2. if there is no texture for the given layer, `-1` is returned</sub><br />
<sub>3. the midground texture rotates with the knob, while the background and foreground are static</sub><br />

`/set/module_structure/param/slider`: Slider-specific parameter information
* A slider has a track, and a handle that moves within the bounding box of the track. It can be oriented horizontally or vertically.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | float | handle size x | handle width |
| 3 | float | handle size y | handle height |
| 4 | float | min handle pos x | handle x position at minimum value, relative to param position |
| 5 | float | min handle pos y | handle y position at minimum value, relative to param position |
| 6 | float | max handle pos x | handle x position at maximum value, relative to param position |
| 7 | float | max handle pos y | handle y position at maximum value, relative to param position |
| 8 | bool | horizontal | true = horizontal, false = vertical |
| 9 | int64 | track texture id | unique id for requesting a render of the slider's track |
| 10 | int64 | handle texture id | unique id for requesting a render of the slider's handle |


`/set/module_structure/param/button`: Button-specific parameter information.
A button may be momentary (returns to unpressed when released) or
toggle/latching (stays pressed until pressed again).

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | bool | momentary | true = momentary, false = toggle/latching |
| 3… | int64 | frame texture id | one texture id per parameter position, in increasing value order |


`/set/module_structure/param/switch`: Switch-specific parameter information
* A switch has an arbitrary number of positions. It cycles through them in order, wrapping back around to the first position after the last. It can be oriented horizontally or vertically.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | int32 | num positions | number of positions (`max value - min value + 1`) |
| 3 | bool | horizontal | true = horizontal, false = vertical |
| 4… | int64 | frame texture id | one texture id per position, in increasing parameter-value order |

Button frame texture ids use the same increasing parameter-value order; for a
two-state button, the first is unpressed and the second is pressed.

---
**Port definitions:** One message per port. Positions are relative to module position.

`/set/module_structure/port`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this port belongs |
| 1 | int32 | port id | unique input or output id for the structure of the module to which this port belongs |
| 2 | int32 | port type | 1 = input, 2 = output |
| 3 | string | name | port name |
| 4 | string | description | port description |
| 5 | float | size x | width of the port's bounding box |
| 6 | float | size y | height of the port's bounding box |
| 7 | float | pos x | x position, relative to module position |
| 8 | float | pos y | y position, relative to module position |
| 9 | bool | visible | default visibility |
| 10 | int64 | texture id | unique id for requesting a render of the port |

---
**Light definitions:** One message per light<sup>2</sup>. Positions are relative to module position.

`/set/module_structure/light`
| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | structure id | unique id for the structure of the module to which this light belongs |
| 1 | int32 | light id | light id for this structure; also used by light-state updates<sup>1</sup> |
| 2 | int32 | parameter id | parameter id of the parameter this light is "attached" to (such as a button or slider handle), `-1` if independent |
| 3 | int32 | shape | 1 = round, 2 = rectangular |
| 4 | float | size x | width of the light's bounding box |
| 5 | float | size y | height of the light's bounding box |
| 6 | float | pos x | x position, relative to module position |
| 7 | float | pos y | y position, relative to module position |
| 8 | bool | visible | default visibility |
| 9 | string | background color | RGB hex string (e.g. `#FF0000`) |


<sub>notes:</sub><br />
<sub>1. Light ids are assigned in the order the structure reports grouped lights; they are used by light-state updates.</sub><br />
<sub>2. some modules stack several light widgets in the same spot instead of recoloring one. Widgets with an identical bounding box and parameter id are reported as a single light: its color is the screen blend of the visible widgets' colors (as Rack draws them), its background color is their source-over composite. the light is reported as visible if any of lights in the group are visible.</sub><br />

---

### State

#### `/get/module_state`

Returns the current position and overlay texture for one loaded module.

**Request:** `/get/module_state`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |

**Response:** `/set/state/module` (no response if the module is not loaded)

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |
| 1 | float | position x | module position relative to ctrl's top-left corner |
| 2 | float | position y | module position relative to ctrl's top-left corner |
| 3 | int64 | overlay texture id | texture for runtime graphics drawn over the panel; it may render blank |

#### `/get/params_state`

Requests current parameter state for one module.

**Request:** `/get/params_state`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |

**Response:** One `/set/state/param` message per reported parameter, followed by
`/report/state/param/count`. The first request for a module after server start or a
cache reset reports all its parameters. Later requests report only the
parameters whose value or visibility changed; they are not
full snapshots. No response is sent if the module is not loaded.

Param updates and their count report are sent as a single bundle to ensure
count arrives last.

`/set/state/param`:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |
| 1 | int32 | parameter id | parameter index from `/get/module_structure` |
| 2 | bool | visible | current visibility |
| 3 | float | value | current parameter value |
| 4 | string | label | current formatted parameter value (shown in the param's tooltip in rack) |

**`/report/state/param/count`**

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | update count | number of param updates |
---

### Parameter Control

#### `/set/param/value`

Sets a parameter value. Rack clamps or otherwise handles the value according to
the parameter's `ParamQuantity`.

**Request:** `/set/param/value`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |
| 1 | int32 | parameter id | parameter index from `/get/module_structure` |
| 2 | float | value | requested parameter value |
| 3 | bool | needs acknowledgement | when true, request an `/ack/param/value` response |

**Response:** If `needs acknowledgement` is true, `/ack/param/value`:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | requested module id |
| 1 | int32 | parameter id | requested parameter id |
| 2 | float or int32 | value | applied value on success; integer `-1` on failure |
| 3 | bool | success | false if the module or parameter was not found |

#### `/param/click`, `/param/press`, `/param/release`

Synthesizes left-button interactions on the parameter's widget, on the UI
thread. Targets are resolved by ID even when offscreen or hidden; the Rack view
is not scrolled or zoomed. Synthetic gestures use separate event state, so they
do not capture desktop mouse movement, lock its cursor, or change its selection.

**Request:** Each route takes two arguments:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |
| 1 | int32 | parameter id | parameter index from `/get/module_structure` |

`/param/press` presses and holds the target. Only one synthetic hold is active:
another press releases the previous target first, even if it is the same
parameter. `/param/click` also releases any existing synthetic hold, then presses
its target and schedules a release on a subsequent UI tick, after the engine has
advanced enough to give the module an opportunity to observe the press.

Release/repress transitions are serialized with engine-visible gaps so successive
clicks are not merged. An explicit release received before the engine has
observed its press is also deferred. While the engine is stopped, these pending
transitions and their acknowledgements wait for engine progress.

`/param/release` releases only a matching synthetic hold. If there is no matching
hold, it succeeds without sending an event. Duplicate releases are therefore
harmless; duplicate clicks and presses are **not** deduplicated.

An active real mouse drag is not interrupted. Holds are cleared when the
client disconnects or resets, ctrl is removed, or the target module is removed.
If the native mouse takes over the same control, its gesture is left intact and
the synthetic gesture is canceled.

**Response:** Every request receives its corresponding `/ack/param/click`,
`/ack/param/press`, or `/ack/param/release`. ACKs are sent after the requested
transition is dispatched; click ACKs are sent after the deferred release,
not merely after the request is queued.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | requested module id |
| 1 | int32 | parameter id | requested parameter id |
| 2 | bool | success | false if the target or UI event state is unavailable, or a press/click conflicts with a real pointer interaction |

---

### Cable Management

#### `/get/cables`

Returns all patch cables.

**Request:** `/get/cables` (no arguments)

**Response:** One `/set/cable` per cable, followed by `/report/cable/count`.

Cables and their count report are sent as a single bundle to ensure
count arrives last.

`/set/cable`:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | cable id | Rack's unique id for the cable |
| 1 | int64 | input module id | module connected to the cable's input |
| 2 | int64 | output module id | module connected to the cable's output |
| 3 | int32 | input port id | input port index |
| 4 | int32 | output port id | output port index |
| 5 | string | color | cable color as a hex string |

`/report/cable/count`:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | cable count | number of cables in the patch |

#### `/add/cable`

Creates a cable. If the same connection already exists, the request succeeds
and returns the existing cable id.

**Request:** `/add/cable`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | input module id | module connected to the cable's input |
| 1 | int64 | output module id | module connected to the cable's output |
| 2 | int32 | input port id | input port index |
| 3 | int32 | output port id | output port index |
| 4 | string | color | cable color as a hex string |
| 5 | int64 | return id (optional) | echoed in the acknowledgement; defaults to `-1` |

**Response:** `/ack/cable/add`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | return id | value supplied in the request, or `-1` if omitted |
| 1 | int64 | cable id | created or existing id; `-1` on failure |
| 2 | bool | success | false if a module or port was not found |

#### `/remove/cable`

Removes a cable.

**Request:** `/remove/cable`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | cable id | Rack's unique id for the cable |

**Response:** `/ack/cable/remove`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | cable id | requested cable id |
| 1 | bool | success | false if the cable was not found |

---

### Subscriptions (Real-time Streaming)

#### `/subscribe/module/lights`

Subscribes to light state updates for a loaded module.

**Request:** `/subscribe/module/lights`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | Rack's unique id for the module instance |

**Response:** `/ack/subscribe/module/lights`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | requested module id |
| 1 | bool | success | false if the module is not loaded |

On success, `/set/state/lights` updates are produced each Rack UI frame. The first
update reports every light; later updates include only changed lights. Unsent
updates may be coalesced.

To avoid message-parsing overhead, all light updates are sent in a single
message (ie, index 0 is madule id, as is index 4, index 8, and so on).
The client should loop over the indidces until no more data remains.

A 32-bit integer uses less message space than a string holding a hex value, so
colors are packed into a 32-bit integer in the form `0xAARRGGBB`.
Unpack with, eg.:
```python
a = (color_int >> 24) & 0xFF
r = (color_int >> 16) & 0xFF
g = (color_int >> 8)  & 0xFF
b = color_int         & 0xFF
```

There is no unsubscribe request. The subscription ends when the connection is
reset, including after heartbeat timeout.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | module containing the light |
| 1 | int32 | light id | light index from `/get/module_structure` |
| 2 | bool | visible | current visibility |
| 3 | int32 | color | packed `0xAARRGGBB` color |

---

### Texture Rendering

#### `/get/texture`

Renders a panel, overlay, port, or parameter layer/frame.

**Request:** Use one of these argument forms:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | texture id | id from structure/state metadata; ids must be positive |
| 1 | int32 | sequence id | client-chosen id echoed in every chunk |
| 2 | float | scale | size multiplier relative to Rack dimensions |

Or, for pixel dimensions:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | texture id | id from structure/state metadata; ids must be positive |
| 1 | int32 | sequence id | client-chosen id echoed in every chunk |
| 2 | int32 | height | target height in pixels |
| 3 | int32 | width (optional) | target width; when omitted, aspect ratio is preserved |

**Response:** Chunked `/set/texture` messages (see Chunked Transfer Protocol).

**Notes:**
- A texture id of `-1` means there is no texture; do not request it.
- Overlays are rendered on each request, so each result reflects the module at
  roughly the time the request is handled.
- Requests are processed on the Rack UI thread. A queued request with the same
  texture id and requested size arguments is coalesced: the later sequence id
  replaces the earlier one, which receives no reply.
- A non-positive or unknown id, or a render failure, produces no reply.

---

### Chunked Transfer Protocol

The rendered image is RGBA with top-down rows, encoded as
[QOI](https://qoiformat.org/). Each chunk is sent in its own UDP packet; chunks
are queued and may be paced.

**Server sends:** `/set/texture`, once per chunk.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | texture id | texture being transferred |
| 1 | int32 | sequence id | echoed from the request |
| 2 | int32 | chunk number | zero-based chunk index |
| 3 | int32 | number of chunks | total chunks in this image |
| 4 | int32 | chunk size | bytes per chunk, except possibly the last |
| 5 | int64 | total size | total encoded QOI size in bytes |
| 6 | int32 | width | rendered image width in pixels |
| 7 | int32 | height | rendered image height in pixels |
| 8 | blob | chunk data | bytes for offset `chunk number × chunk size` |

**Client sends:** `/ack_chunk` for every copy of every received chunk.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | texture id | texture being transferred |
| 1 | int32 | sequence id | sequence id from the chunk |
| 2 | int32 | chunk number | chunk to acknowledge |

**Delivery:**
- Chunks can arrive out of order or more than once. Acknowledge every copy and
  place its data at the indicated offset.
- Unacknowledged chunks are retried. The timeout adapts to measured round trips,
  is capped at 200 ms, and backs off after a timeout. Retries are checked on
  Rack UI frames.
- A transfer is abandoned if a chunk remains unacknowledged 1.2 seconds after
  its first send.
- A later prepared render of the same texture id and pixel dimensions
  supersedes the older in-flight transfer. Use sequence ids to identify chunks
  and discard stale partial or completed results.
- The image is complete when all chunks have arrived; decode the `total size`
  bytes as QOI.

---

### Audio (Tx expander)

A Tx module directly to the left or right of ctrl is managed by ctrl. Its
standalone host/port settings are ignored while managed. Audio streams only
after the registered client enables it. The Tx's controls and
`/set/audio/preset` share the same preset. Audio uses its own UDP socket, not
OSC.

#### `/set/audio_stream`

Server announcement describing the managed Tx stream.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | managed Tx module id |
| 1 | int32 | stream id | read as uint32; matches the audio packet header |
| 2 | int32 | preset id | 0 = Lowest latency, 1 = Balanced, 2 = Highest quality, 3 = Uncompressed |
| 3 | int32 | enabled | 0 = disabled, 1 = enabled |
| 4 | int32 | epoch | per-instance random u16 seed; may lag the packets by one, so packets are authoritative |

While in direct mode, sent on registration, Tx attach/detach, successful audio
requests, and preset changes made in Rack. While connected and a Tx is managed,
it is re-announced every two heartbeats (about 2 seconds). Repeated identical
announcements are idempotent and should not restart playback.

#### `/set/audio/enabled`

Enables or disables the stream to the registered client's address.

**Request:**

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | managed Tx module id |
| 1 | int32 | enabled | 0 = disable, 1 = enable |
| 2 | int32 | port | client's UDP audio port (normally 7747 when enabling) |

**Response:** `/set/audio/enabled/ack`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | requested module id |
| 1 | int32 | enabled | requested state |
| 2 | int32 | success | 0 = failed, 1 = succeeded |

The request fails if the module is not the managed Tx, no client is registered,
or an enabled request supplies an invalid port. Registering again or missing
heartbeats stops the stream; enable it again after `/register`.

#### `/set/audio/preset`

Selects the latency/quality preset.

**Request:**

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | managed Tx module id |
| 1 | int32 | preset id | 0 = Lowest latency, 1 = Balanced, 2 = Highest quality, 3 = Uncompressed |

**Response:** `/set/audio/preset/ack`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | requested module id |
| 1 | int32 | preset id | requested preset |
| 2 | int32 | success | 0 = failed, 1 = succeeded |

The request fails if the module is not the managed Tx, no client is registered,
or the preset id is outside 0–3.

#### `/audio/stats`

Report jitter-buffer health to Tx. Send every ~500 ms while playing.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | managed Tx module id |
| 1 | int32 | underruns | cumulative count for this client stream |
| 2 | int32 | lost | cumulative lost-frame count |
| 3 | int32 | recovered | cumulative recovered-frame count |
| 4 | float | jitter ms | 95th-percentile arrival jitter |
| 5 | float | buffer ms | current buffer level |

There is no response. New underruns/losses or a jitter spike lower the texture
chunk-rate cap; clean reports raise it incrementally. If no stats arrive for 2
seconds, the cap expires and chunk sends become unpaced.

#### Audio packet format

Sent by UDP to the client's port, one datagram (≤ 1232 bytes) per packet. All
fields are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `RA` |
| 2 | 1 | version (1) |
| 3 | 1 | flags (0) |
| 4 | 4 | stream id (u32) |
| 8 | 2 | epoch (u16); a new epoch means flush and reset the decoder. An older epoch (serial order) is a straggler only while the current one is live (packet within 250 ms) |
| 10 | 1 | codec (0 = PCM s16le, 1 = Opus) |
| 11 | 1 | channels (2) |
| 12 | 2 | frame samples per channel at 48 kHz |
| 14 | 1 | frame count |
| 15 | 1 | preset id |
| 16 | 4 | sequence number of the newest frame |
| 20 | … | `frame count` × (u16 length, bytes), newest first; frame i has sequence `newest − i` |

For redundancy, older frames repeat earlier packets so a single lost packet
can be recovered.

---

### Benchmark-only API

These routes are available only in builds compiled with `GTNOSFT_BENCH`.

#### `/bench/reset` and `/bench/report`

Both requests take no arguments.

`/bench/reset` starts a new measurement window and responds with
`/bench/reset/ack`:

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | generation | id of the new measurement window |

`/bench/report` responds with these messages:

| Path | Arguments |
|---|---|
| `/bench/report/begin` | `int64` generation, `float` window seconds |
| `/bench/stat` | `string` name, `int32` count, `float` mean, min, p50, p95, p99, max |
| `/bench/counter` | `string` name, `int64` value |
| `/bench/texture` | `int64` texture id, `string` kind, `int32` completed, `int32` failed, `float` fps |
| `/bench/report/end` | `int32` total message count, including this message |

#### Benchmark toggles

Each toggle request takes one `int32` argument (`0` = disabled, nonzero =
enabled) and responds at the request path plus `/ack` with the resulting
`int32` state.

| Request path |
|---|
| `/bench/overlay_cache` |
| `/bench/prep_worker` |
| `/bench/adaptive_retry` |
| `/bench/back_pressure` |
| `/bench/heartbeat_direct` |
| `/bench/action_budget` |
| `/bench/supersede` |
| `/bench/chunk_pacing` |

---
