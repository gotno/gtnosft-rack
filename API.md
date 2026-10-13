# OSCctrl API Documentation

## Overview

OSCctrl provides a bidirectional [OSC (Open Sound Control)](https://ccrma.stanford.edu/groups/osc/index.html) network API for controlling and monitoring VCV Rack, enabling remote control, state synchronization, and visual rendering of modules and their components.

!! something about all messages being bundled

## Connection Flow
### 1. Discovery Phase (Broadcast Mode)

OSCctrl starts in **broadcast mode**, broadcasting a message on the local network at regular intervals, announcing its presence and providing the information needed to register. In broadcast mode, the server has only a single route, `/register`.

#### Announcement

**Broadcast:** `/announce` on port 7746

| index | type | contents | description |
|---:|---|---|---|
| 0 | int32 | listen port | the port the on which the server is listening for incoming messages |
| 1 | int32 | interval | the interval between heartbeat messages in milliseconds |


**Your client should:**
1. Listen for `/announce` broadcasts on port 7746
2. Extract the listen port and interval from the message, and record the IP address the message came from.
3. Register for directed communication (continue to step 2)

### 2. Registration Phase

Once you've discovered the server, register to switch to **direct unicast mode**.

**Request:** `/register`
* (no arguments)

**The server will:**
- Switch to direct unicast mode (sends only to your IP)
- Start listening for other messages

### 3. Heartbeat Maintenance

After registration, you may maintain the connection by sending periodic heartbeats. Once OSCctrl has received a heartbeat, it will start watching for subsequent heartbeats. If it misses 5 consecutive heartbeats, it will switch back to broadcast mode and cancel any subscriptions.

**Request:** `/keepalive`
* (no arguments)
* Frequency: heartbeat interval received in `/announce` (more frequently is fine)

**The server will:**
- Start counting consecutive intervals with no heartbeat received
- Start sending `/heartbeat` with CPU stats at the same interval

**After 5 missed heartbeats, the server will switch back to broadcast mode and cancel any subscriptions**

**Response:** `/heartbeat`

| index | type | contents | description |
|---:|---|---|---|
| 0 | float | CPU avg | average CPU load percentage, 0-100 |
| 1 | float | CPU max | maximum CPU load percentage, 0-100 |

---

## API Reference

### General Notes
1. all size and position values are in centimeters
2. position represents the top-left corner of a bounding box
3. for position and size, positive x values move rightward, positive y values move downward

### Connection Management

#### `/register`
* (no arguments)
In broadcast mode, register the client's IP address to switch to direct unicast mode.

#### `/keepalive`
* (no arguments)
In direct unicast mode, switch connection monitoring on. The server will then watch for repeat `/keepalive` messages and, if it stops receiving them, switch back to broadcast mode and cancel any subscriptions.

---
### Patch Management
#### Patch info `/get/patch_info`
Request current patch information. The OSCctrl module id can be useful for disambiguating never-saved patches or patches that share a filename.

**Request:** `/get/patch_info`
* (no arguments)

**Response:** `/set/patch_info`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | OSCctrl id | module id of OSCctrl module |
| 1 | string | filename | current patch filename (empty string for never-saved patch) |


---
#### Open a patch `/patch/open`
Loads a patch from the server's local filesystem.

**Request:** `/patch/open`

| index | type | contents | description |
|---:|---|---|---|
| 0 | string | path | absolute filesystem path to .vcv patch file |


**Response:** None

---

### Module Discovery
#### Get loaded modules `/get/module_stubs`
Get the id, plugin slug, and module slug for each module in the patch.

**Request:** `/get/module_stubs`
* (no arguments)

**Response:** `/set/module_stub` (one message for each module)

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | module id | rack's unique module id for the module instance |
| 1 | string | plugin slug | rack's unique library slug for the module's plugin, eg 'gotnosoft' |
| 2 | string | module slug | rack's unique library slug for the module, eg 'OSCctrl' |
---

### Structure
Structure represents the general blueprint for a module and its params, ports and lights. It is distinct from State, which is the data unique to a specific instance of a module in the patch.

**Request:** `/get/module_structure`

| index | type | contents | description |
|---:|---|---|---|
| 0 | string | plugin slug | rack's unique library slug for the module's plugin, eg 'gotnosoft' |
| 1 | string | module slug | rack's unique library slug for the module, eg 'OSCctrl' |


**Response:** Multiple messages describing the module and its parameters, ports, and lights.

**Module header:** `/set/module_structure` General module information.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for this module structure definition, also sent with payloads for the module's params, ports, and lights |
| 1 | string | plugin slug | rack's unique library slug for the module's plugin, eg 'gotnosoft' |
| 2 | string | module slug | rack's unique library slug for the module, eg 'OSCctrl' |
| 3 | float | size x | width of the module |
| 4 | float | size y| height of the module |
| 5 | int64 | panel texture id | unique id for requesting a render of this module's panel |
| 6 | int32 | num params | total number of parameters (knobs, sliders, buttons, and switches) for this module |
| 7 | int32 | num inputs | total number if input ports for this module |
| 8 | int32 | num outputs | total number of output ports for this module |
| 9 | int32 | num lights | total number of lights for this module |

---
**Parameter definitions:** Two messages per parameter: One general info and one type-specific info. Positions are relative to module position.

**Parameter header:** `/set/module_structure/param`: Base parameter information
Shared data shape for all param types.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter index for this module |
| 2 | int32 | param type | 1 = knob, 2 = slider, 3 = button, 4 = switch |
| 3 | string | name | parameter name |
| 4 | string | description | parameter description |
| 5 | float | pos x | x position, relative to module position |
| 6 | float | pos y | y position, relative to module position |
| 7 | float | size x | width of the parameter's bounding box |
| 8 | float | size y | height of the parameter's bounding box |
| 9 | float | default value | parameter's default value |
| 10 | float | min value | parameter's minimum value |
| 11 | float | max value | parameter's maximum value |
| 12 | bool | visible | parameter's default visibility (true = visible) |
| 13 | bool | snap | whether the parameter snaps to integer values |


`/set/module_structure/param/knob`: Knob-specific parameter information

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 3 | float | min angle | minimum rotation angle in radians<sup>1</sup> |
| 4 | float | max angle | maximum rotation angle in radians<sup>1</sup> |
| 5 | int64 | background texture id | unique id for requesting a render of the knob's background<sup>2 3</sup> |
| 6 | int64 | midground texture id | unique id for requesting a render of the knob's midground<sup>2 3</sup> |
| 7 | int64 | foreground texture id | unique id for requesting a render of the knob's foreground<sup>2 3</sup> |


<sub>notes:</sub><br />
<sub>1. zero is at 12 o'clock, positive is clockwise</sub><br />
<sub>2. if there is no texture for the given layer, `-1` is returned</sub><br />
<sub>3. the midground texture rotates with the knob, while the background and foreground are static</sub><br />

`/set/module_structure/param/slider`: Slider-specific parameter information
* A slider has a track, and a handle that moves within the bounding box of the track. It can be oriented horizontally or vertically.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | float | handle size x | handle width |
| 3 | float | handle size y | handle height |
| 4 | float | min handle pos x | handle x position at minimum value, relative to param position |
| 5 | float | min handle pos y | handle y position at minimum value, relative to param position |
| 6 | float | max handle pos x | handle x position at maximum value, relative to param position |
| 7 | float | max handle pos y | handle y position at maximum value, relative to param position |
| 8 | bool | orientation | 0 = horizontal, 1 = vertical |
| 9 | int64 | track texture id | unique id for requesting a render of the slider's track |
| 10 | int64 | handle texture id | unique id for requesting a render of the slider's handle |


`/set/module_structure/param/button`: Button-specific parameter information
* A button has two states, pressed and unpressed. It can be momentary (returns to unpressed when released) or toggle/latching (stays pressed until pressed again).

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | bool | momentary | 0 = momentary, 1 = toggle/latching |
| 3 | int64 | unpressed texture id | unique id for requesting a render of the buttons's unpressed texture |
| 4 | int64 | pressed texture id | unique id for requesting a render of the buttons's pressed texture |


`/set/module_structure/param/switch`: Switch-specific parameter information
* A switch has an arbitrary number of positions. It cycles through them in order, wrapping back around to the first position after the last. It can be oriented horizontally or vertically.

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | param id | the parameter's index in this module's parameter list |
| 2 | int32 | num positions | number of positions (max value + 1) |
| 3 | bool | orientation | 0 = horizontal, 1 = vertical !!TODO: necessary? |
| 4 | int64 | frame texture id | unique id for requesting render of the switch's 1st position texture |
| 5 | int64 | frame texture id | unique id for requesting render of the switch's 2nd position texture |
| 6-n | int64 | frame texture id | and so on... |

---
**Port definitions:** One message per port. Positions are relative to module position.

`/set/module_structure/port`

| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
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
**Light definitions:** One message per light. Positions are relative to module position.

`/set/module_structure/light`
| index | type | contents | description |
|---:|---|---|---|
| 0 | int64 | structure id | unique id for the structure of the module to which this param belongs |
| 1 | int32 | light id | unique light id for the structure of the module to which this port belongs<sup>1</sup> |
| 2 | int32 | parameter id | parameter id of the parameter this light is "attached" to (such as a button or slider handle), `-1` if independent |
| 3 | int32 | shape | 1 = round, 2 = rectangular |
| 5 | float | size x | width of the light's bounding box |
| 6 | float | size y | height of the light's bounding box |
| 7 | float | pos x | x position, relative to module position |
| 8 | float | pos y | y position, relative to module position |
| 9 | bool | visible | default visibility |
| 3 | string | background color | rgb hex string (eg "#FF0000") |


<sub>notes:</sub><br />
<sub>1. unlike parameters and ports, this id is not something Rack assigns, but it will be useful later when receiving light updates via subscription</sub><br />

---

### State

#### `/get/module_state <moduleId>`
**Direction:** Client → Server
**Purpose:** Get state-specific information for an individual module instance
**Arguments:**
  - `int64` moduleId

**Response:**
```
Path: /set/s/m
Arguments:
  - int64: moduleId
  - float: posX - Module position X in rack
  - float: posY - Module position Y in rack
  - int64: overlay texture id (any runtime graphics drawn on top of the main panel svg)
    - for many modules, this texture will be empty
    - for some modules, this will be the display (eg VCV Audio or Audible Instruments Macro Oscillator)
    - some modules use this to draw labels for params and ports
    - for animated modules (eg, VCV Scope), each request for this texture will give you the animation at roughly the moment the request is received.
```

#### `/get/params_state <moduleId>`
**Direction:** Client → Server
**Purpose:** Get state-specifc parameter information values for a module
**Arguments:**
  - `int64` moduleId

**Response:**
```
Path: /set/s/p
Arguments:
  - int64: moduleId
  - ...parameter values (one float per parameter)
```

---

### Parameter Control

#### `/set/param/value <moduleId> <paramId> <value>`
**Direction:** Client → Server
**Purpose:** Set a parameter value
**Arguments:**
  - `int64` moduleId
  - `int32` paramId
  - `float` value

**Response:** None (value applied immediately)

**Usage:**
- Parameter IDs come from `/get/module_structure`
- Value should be in parameter's min/max range
- Updates happen in real-time

---

### Cable Management

#### `/get/cables`
**Direction:** Client → Server
**Purpose:** Get all patch cables
**Arguments:** None
**Response:** One `/set/cable` per cable

```
Path: /set/cable
Arguments:
  - int64: cableId
  - int64: inputModuleId
  - int64: outputModuleId
  - int32: inputPortId
  - int32: outputPortId
  - string: color - Hex color (e.g., "#ff0000")
  - int64: returnId - Echo of requested ID (or 0 if not requested)
```

#### `/add/cable <inputModuleId> <outputModuleId> <inputPortId> <outputPortId> <color> [returnId]`
**Direction:** Client → Server
**Purpose:** Create a patch cable
**Arguments:**
  - `int64` inputModuleId
  - `int64` outputModuleId
  - `int32` inputPortId
  - `int32` outputPortId
  - `string` color - Hex color
  - `int64` returnId (optional) - ID to echo back in response

**Response:**
```
Path: /set/cable
Arguments: (same as /get/cables response, with your returnId)
```

#### `/remove/cable <cableId>`
**Direction:** Client → Server
**Purpose:** Delete a patch cable
**Arguments:**
  - `int64` cableId

**Response:** None (cable removed)

---

### Subscriptions (Real-time Streaming)

#### `/subscribe/module/lights <moduleId>`
**Direction:** Client → Server
**Purpose:** Subscribe to real-time light state updates
**Arguments:**
  - `int64` moduleId

**Response:** Continuous stream of:
```
Path: /set/s/l
Arguments:
  - int64: moduleId
  - ...light brightness values (one float per light, 0.0-1.0)
Frequency: Every 30ms while subscribed
```

**Note:** Subscription stays active until client disconnects or heartbeat times out.

---

### Texture Rendering

#### `/get/texture <textureId> <sequenceId> <scale|height> [width]`
**Direction:** Client → Server
**Purpose:** Render a texture: a panel, overlay, port, or one layer or frame of a param
**Arguments:**
  - `int64` textureId - from `/set/module_structure` (panel, params, ports) or `/set/s/m` (overlay)
  - `int32` sequenceId - your id for this request; echoed in every chunk of the reply
  - size, one of:
    - `float` scale - multiplier of the widget's size in Rack (e.g. 3.0)
    - `int32` height - target height in pixels; width keeps the aspect ratio
    - `int32` height, `int32` width - exact size in pixels (may stretch)

**Response:** a chunked image via `/set/texture` (see Chunked Transfer Protocol below)

**Notes:**
- Texture ids of `-1` mean there is no texture (e.g. a missing knob layer); don't request them.
- Overlays (any runtime graphics drawn on top of the panel) are re-rendered on every request, so each reply shows the module at roughly the moment the request is handled.
- Requests are handled once per Rack UI frame. If a request for the same texture id and size is still queued when a new one arrives, the newer `sequenceId` replaces it and the older one gets **no reply**.
- No reply is sent if the id is unknown or rendering fails (see Rack's `log.txt`).

---

### Chunked Transfer Protocol

The rendered image is RGBA, rows top-down, encoded as [QOI](https://qoiformat.org/). The encoded bytes are split into chunks that each fit in one UDP packet, and **all chunks are sent at once**.

**Server sends, once per chunk:**
```
Path: /set/texture
Arguments:
  - int64: textureId
  - int32: sequenceId - from your request
  - int32: chunkNum - 0-indexed
  - int32: numChunks
  - int32: chunkSize - bytes per chunk (all but the last are this size)
  - int64: totalSize - bytes of QOI data
  - int32: width - image width in pixels
  - int32: height - image height in pixels
  - blob: chunk data, which belongs at offset chunkNum * chunkSize
```

**Client sends for every chunk received:**
```
Path: /ack_chunk
Arguments:
  - int64: textureId
  - int32: sequenceId
  - int32: chunkNum
```

**Delivery:**
- Chunks can arrive out of order, and the same chunk can arrive more than once. Ack every copy, and place data by offset.
- Unacked chunks are resent after a timeout that adapts to the measured round trip (roughly one or two UI frames, at most 200 ms), backing off on each resend.
- A transfer is abandoned if a chunk is still unacked 1.2 s after it was first sent.
- A transfer also stops, possibly partway, once a newer frame of the same texture id and size is ready to send. Discard a partial frame when a newer `sequenceId` for that texture and size arrives, and ignore an older frame that completes after a newer one.
- The image is complete when all `numChunks` chunks have arrived; decode the `totalSize` bytes as QOI.

---

## Typical Usage Flow

### Initial Setup

```
1. Listen for /announce broadcasts on port 7746
   → Extract RX port (usually 7225)

2. Send /register to RX port
   → Server switches to direct mode

3. Start heartbeat loop: send /keepalive every 500ms
   → Maintains connection

4. Send /get/patch_info
   ← Receive /set/patch_info with ctrlId and filename

5. Send /get/module_stubs
   ← Receive /set/module_stub for each module
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
   → ACK each chunk with /ack_chunk
   → Assemble and decode the QOI image

3. Cache this information - only needs to be done once per module type
```

### Getting Module State

```
For each module instance:

1. Send /get/module_state <moduleId>
   ← Receive /set/s/m with position and overlay texture id

2. Send /get/texture <overlayTextureId> <sequenceId> <scale|height>
   ← Receive chunks of the module's current overlay (displays, etc.)

3. Optional: Send /subscribe/module/lights <moduleId>
   ← Receive /set/s/l every 30ms with light brightness values
```

### Controlling Parameters

```
When user interacts with virtual knob/slider:

1. Calculate new parameter value (within min/max from structure)

2. Send /set/param/value <moduleId> <paramId> <value>
   → Value applied in Rack immediately

3. Optional: Request a new overlay render to see the updated display
   Send /get/texture <overlayTextureId> <sequenceId> <scale|height>
```

### Managing Cables

```
To add a cable:
1. Send /add/cable <inputModuleId> <outputModuleId> <inputPortId> <outputPortId> "#ff0000" <returnId>
   ← Receive /set/cable with new cableId and your returnId

To get all cables:
1. Send /get/cables
   ← Receive /set/cable for each existing cable

To remove a cable:
1. Send /remove/cable <cableId>
```

---

## Error Handling

### Connection Errors

- **Port unavailable:** Server auto-increments RX port up to 7244. Check `/announce` for actual port.
- **Heartbeat missed:** After 5 seconds (5 missed heartbeats), server switches to broadcast mode. Re-register.
- **No broadcast received:** Check firewall, network configuration, ensure server and client on same network.

### Request Errors

Errors are logged in VCV Rack's log.txt but not sent to client. Common issues:

- **Invalid moduleId:** Module not found (might have been deleted)
- **Invalid paramId:** Parameter doesn't exist for that module
- **Invalid pluginSlug/moduleSlug:** Module type doesn't exist
- **Render failure:** Widget couldn't be created or rendered (check log.txt)

### Chunked Transfer Errors

- **Missing chunks:** Resent automatically until acked; ack every chunk you receive
- **Timeout:** Transfer abandoned if a chunk is unacked 1.2 s after its first send
- **Wrong ids in ACK:** textureId and sequenceId must match the ones in `/set/texture`
- **No reply at all:** Unknown texture id, render failure, or the request was replaced by a newer one for the same texture and size

---

## Performance Considerations

### Rendering

- **Panel textures:** Cache these - they're static per module type
- **Overlay textures:** Expensive to render - don't request every frame
- **Recommended overlay update rate:** 10-30 FPS max
- **Use subscriptions:** `/subscribe/module/lights` is more efficient than polling

### Network

- **UDP is unreliable:** Packet loss is possible - the chunked protocol resends lost chunks
- **Heartbeat timing:** Keep under 1000ms to avoid disconnection
- **Concurrent requests:** Limit simultaneous texture requests to avoid overload

### Subscriptions

- **Light updates:** Sent every 30ms (33 FPS) per subscribed module
- **Automatic cleanup:** Subscriptions end when client disconnects
- **Selective subscription:** Only subscribe to modules user is viewing

---

## Example Message Sequences

### Complete Module Setup

```
Client → Server: /get/module_structure "Fundamental" "VCO-1"
Server → Client: /set/module_structure 1 "Fundamental" "VCO-1" 7.5 38.1 6 1 2 3
Server → Client: /set/module_structure/param/knob 1 0 "Frequency" 0.0 1.0 ...
Server → Client: /set/module_structure/param/knob 1 1 "Fine" 0.0 1.0 ...
... (more params) ...
Server → Client: /set/module_structure/port 1 0 0 "V/Oct" ...
Server → Client: /set/module_structure/port 1 0 1 "Sine" ...
... (more ports) ...
Server → Client: /set/module_structure/light 1 0 -1 0 1.0 1.0 ...
... (more lights) ...

(panel texture id from /set/module_structure, e.g. 7001)
Client → Server: /get/texture 7001 100 3.0
Server → Client: /set/texture 7001 100 0 3 1416 3900 1024 768 <chunk 0>
Server → Client: /set/texture 7001 100 1 3 1416 3900 1024 768 <chunk 1>
Server → Client: /set/texture 7001 100 2 3 1416 3900 1024 768 <chunk 2>
Client → Server: /ack_chunk 7001 100 0
Client → Server: /ack_chunk 7001 100 1
Client → Server: /ack_chunk 7001 100 2
(Client assembles and decodes the QOI image)
```

### Parameter Change

```
Client → Server: /set/param/value 42 0 0.75
(Parameter immediately updated in Rack)

(overlay texture id from /set/s/m, e.g. 9042)
Client → Server: /get/texture 9042 200 2.0
Server → Client: /set/texture 9042 200 0 1 1416 1200 512 384 <data>
Client → Server: /ack_chunk 9042 200 0
(Client displays the updated overlay)
```

### Real-time Light Monitoring

```
Client → Server: /subscribe/module/lights 42
(Every 30ms:)
Server → Client: /set/s/l 42 0.0 1.0 0.5 ...
Server → Client: /set/s/l 42 0.0 0.9 0.6 ...
Server → Client: /set/s/l 42 0.1 0.8 0.7 ...
...
```

---

## Constants Reference

```cpp
// Network
RX_PORT = 7225              // Server receive port (can increment to 7244)
TX_PORT = 7746              // Server transmit port
MSG_BUFFER_SIZE = 1452      // Max UDP packet size

// Timing
HEARTBEAT_INTERVAL_MS = 1000      // Milliseconds between heartbeats
MAX_MISSED_HEARTBEATS = 5   // Missed before switching to broadcast
SUBSCRIPTION_SEND_DELAY = 30 // Milliseconds between subscription updates

// Port Types
PORT_INPUT = 0
PORT_OUTPUT = 1

// Light Shapes
LIGHT_ROUND = 0
LIGHT_RECTANGLE = 1
```

---

## OSC Data Type Reference

- `int32` - 32-bit signed integer
- `int64` - 64-bit signed integer
- `float` - 32-bit floating point
- `string` - Null-terminated UTF-8 string
- `bool` - Transmitted as int32 (0 = false, non-zero = true)
- `blob` - Binary data with length prefix
