# OSCctrl API Documentation

## Overview

OSCctrl provides a bidirectional OSC (Open Sound Control) network API for controlling and monitoring VCV Rack. This enables remote control, state synchronization, and visual rendering for external applications.

## Network Configuration

- **Server RX Port:** 7225 (auto-increments to 7244 if unavailable)
- **Server TX Port:** 7746
- **Protocol:** UDP (OSC over UDP)
- **Max packet size:** 1452 bytes
- **Data encoding:** PNG images chunked with acknowledgment protocol

## Connection Flow

### 1. Discovery Phase (Broadcast Mode)

The OSCctrl module starts in **broadcast mode**, announcing its presence to the network.

**What the server sends:**
- Message: `/announce`
- Port: 7746 (broadcast to network)
- Frequency: Every 1000ms
- Payload:
  - `int32` - listen port number (7225 by default, but OSCctrl will increment this until it finds an open port)
  - `int32` - The interval at which OSCctrl will be sending heartbeats, and the maximum time between received heartbeats before one is considered missed. (currently defaulted to 1000ms)

**Your client should:**
1. Listen for `/announce` broadcasts on port 7746
2. Extract the listen port and heartbeat interval from the message, and record the IP address the message came from.
3. Register for directed communication (continue to setp 2)

### 2. Registration Phase

Once you've discovered the server, register to switch to **direct unicast mode**.

**Send a message to the server:**
```
Path: /register
Arguments: (none)
```

**The server will:**
- Switch to direct unicast mode (sends only to your IP)
- Start listening for other messages

### 3. Heartbeat Maintenance

After registration, you may maintain the connect by sending periodic heartbeats.

**Send to server:**
```
Path: /keepalive
Arguments: (none)
Frequency: heartbeat interval received in /announce
```

**Initially, the server will:**
- Start counting consecutive intervals with no heartbeat received
- Start sending `/heartbeat` with CPU stats at the same interval

**After that, for each heartbeat, the server will:**
- Reset missed heartbeat counter

**After 5 missed heartbeats, the server will switch back to broadcast mode**

**Server sends (in direct mode):**
```
Path: /heartbeat
Arguments:
  - string: CPU average percentage (e.g., "2.5%")
  - string: CPU max percentage (e.g., "15.0%")
```

---

## API Reference

### Connection Management

#### `/register`
**Direction:** Client → Server
**Purpose:** Switch to direct unicast mode
**Arguments:** None
**Response:** Server begins sending to client IP only

#### `/keepalive`
**Direction:** Client → Server
**Purpose:** Maintain connection
**Arguments:** None
**Frequency:** < 1000ms (recommended: 500-750ms)
**Response:** None (resets timeout counter)

#### `/ack_chunk <chunkId> <chunkNum>`
**Direction:** Client → Server
**Purpose:** Acknowledge receipt of chunked data (images)
**Arguments:**
  - `int32` chunkId - Unique ID for this chunked transfer
  - `int32` chunkNum - Chunk number being acknowledged

**Response:** Server sends next chunk

---

### Patch Management

#### `/get/patch_info`
**Direction:** Client → Server
**Purpose:** Request current patch information
**Arguments:** None
**Response:**
```
Path: /set/patch_info
Arguments:
  - int64: ctrlId - Module ID of OSCctrl module
  - string: filename - Current patch filename
```

#### `/patch/open <path>`
**Direction:** Client → Server
**Purpose:** Load a patch file
**Arguments:**
  - `string` path - Absolute filesystem path to .vcv patch file

**Response:** None (patch loaded in Rack)

---

### Module Discovery

#### `/get/module_stubs`
**Direction:** Client → Server
**Purpose:** List all modules currently in the rack
**Arguments:** None
**Response:** One `/set/module_stub` message per module

```
Path: /set/module_stub
Arguments:
  - int64: moduleId - Unique module instance ID
  - string: pluginSlug - Plugin identifier (e.g., "VCV-VCO")
  - string: moduleSlug - Module identifier (e.g., "VCO-1")
```

**Usage:** Call once after patch loads to get complete module list.

---

### Module Structure

#### `/get/module_structure <pluginSlug> <moduleSlug>`
**Direction:** Client → Server
**Purpose:** Get UI layout and component definitions for a module type
**Arguments:**
  - `string` pluginSlug - Plugin identifier
  - `string` moduleSlug - Module identifier

**Response:** Multiple messages describing module UI:

**1. Structure header:**
```
Path: /set/module_structure
Arguments:
  - int32: structureId - Unique ID for this structure definition
  - string: pluginSlug
  - string: moduleSlug
  - float: panelWidth - Width in cm
  - float: panelHeight - Height in cm
  - int32: numParams - Number of parameters
  - int32: numInputs - Number of input ports
  - int32: numOutputs - Number of output ports
  - int32: numLights - Number of lights
```

**2. Parameter definitions (one per parameter):**
```
Path: /set/module_structure/param/knob (or /button, /switch, /slider)
Arguments:
  - int32: structureId
  - int32: paramId
  - string: name
  - float: min - Minimum value
  - float: max - Maximum value
  - float: posX - Position X in cm
  - float: posY - Position Y in cm
  - float: width - Width in cm
  - float: height - Height in cm

  # Additional for specific types:
  # knob:
    - float: minAngle - Start angle in radians
    - float: maxAngle - End angle in radians
  # slider:
    - bool: horizontal - true if horizontal, false if vertical
  # switch/button:
    - bool: momentary - true if momentary, false if latching
    - int32: numFrames - Number of switch positions
```

**3. Port definitions (one per port):**
```
Path: /set/module_structure/port
Arguments:
  - int32: structureId
  - int32: portId
  - int32: type - 0 = input, 1 = output
  - string: name
  - float: posX
  - float: posY
  - float: width
  - float: height
```

**4. Light definitions (one per light):**
```
Path: /set/module_structure/light
Arguments:
  - int32: structureId
  - int32: lightId
  - int32: paramId - Associated parameter ID, or -1 if independent
  - int32: shape - 0 = round, 1 = rectangle
  - float: width
  - float: height
  - float: posX
  - float: posY
  - bool: visible - Default visibility
  - string: bgColor - Background color hex (e.g., "#000000")
```

**Usage:** Call once per unique module type to understand its UI layout.

---

### Module State

#### `/get/module_state <moduleId>`
**Direction:** Client → Server
**Purpose:** Get complete state for a specific module instance
**Arguments:**
  - `int64` moduleId

**Response:**
```
Path: /set/s/m
Arguments:
  - int64: moduleId
  - float: posX - Module position X in rack
  - float: posY - Module position Y in rack
  - ...parameter values (one float per parameter)
```

#### `/get/params_state <moduleId>`
**Direction:** Client → Server
**Purpose:** Get only parameter values for a module
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

All texture endpoints return PNG images split into chunks. You must acknowledge each chunk with `/ack_chunk` to receive the next one.

**Scale vs Height:**
- Provide either `float scale` OR `int32 height` (not both)
- Scale: Multiplier (e.g., 3.0 = 3x resolution)
- Height: Target height in pixels (width calculated proportionally)

#### `/get/texture/panel <pluginSlug> <moduleSlug> <scale|height> <requestId>`
**Direction:** Client → Server
**Purpose:** Render static module panel (background)
**Arguments:**
  - `string` pluginSlug
  - `string` moduleSlug
  - `float` scale OR `int32` height
  - `int32` requestId - Your unique ID for tracking this request

**Response:** Chunked PNG via `/set/texture` (see Chunked Transfer Protocol below)

#### `/get/texture/overlay <moduleId> <scale|height> <forceRender> <requestId>`
**Direction:** Client → Server
**Purpose:** Render live module state overlay (parameters, lights)
**Arguments:**
  - `int64` moduleId
  - `float` scale OR `int32` height
  - `bool` forceRender - Render even if already processing this requestId
  - `int32` requestId

**Response:** Chunked PNG

**Usage:**
- Panel is static, render once per module type
- Overlay shows live state, render periodically or on subscription updates

#### `/get/texture/port <pluginSlug> <moduleSlug> <portId> <portType> <scale|height> <requestId>`
**Direction:** Client → Server
**Purpose:** Render individual port connector
**Arguments:**
  - `string` pluginSlug
  - `string` moduleSlug
  - `int32` portId
  - `int32` portType - 0 = input, 1 = output
  - `float` scale OR `int32` height
  - `int32` requestId

**Response:** Chunked PNG

#### `/get/texture/knob <pluginSlug> <moduleSlug> <paramId> <scale|height> <bgId> <mgId> <fgId>`
**Direction:** Client → Server
**Purpose:** Render knob layers (background, middle, foreground)
**Arguments:**
  - `string` pluginSlug
  - `string` moduleSlug
  - `int32` paramId
  - `float` scale OR `int32` height
  - `int32` bgId - Request ID for background layer
  - `int32` mgId - Request ID for middle layer
  - `int32` fgId - Request ID for foreground layer

**Response:** Up to 3 chunked PNGs (one per layer that exists)

**Note:** Not all knobs have all three layers. Missing layers are not sent.

#### `/get/texture/switch <pluginSlug> <moduleSlug> <paramId> <scale|height> <requestId1> [requestId2] ...`
**Direction:** Client → Server
**Purpose:** Render all switch positions
**Arguments:**
  - `string` pluginSlug
  - `string` moduleSlug
  - `int32` paramId
  - `float` scale OR `int32` height
  - `int32` requestId... - One request ID per switch position (variable count)

**Response:** Multiple chunked PNGs (one per switch position)

**Usage:** Number of requestIds should match numFrames from module structure.

#### `/get/texture/slider <pluginSlug> <moduleSlug> <paramId> <scale|height> <trackId> <handleId>`
**Direction:** Client → Server
**Purpose:** Render slider track and handle separately
**Arguments:**
  - `string` pluginSlug
  - `string` moduleSlug
  - `int32` paramId
  - `float` scale OR `int32` height
  - `int32` trackId - Request ID for track
  - `int32` handleId - Request ID for handle

**Response:** 2 chunked PNGs (track and handle)

---

### Chunked Transfer Protocol

Large images are sent in chunks with acknowledgment:

**Server sends:**
```
Path: /set/texture
Arguments:
  - int32: requestId - Your request ID
  - int32: chunkNum - Current chunk number (0-indexed)
  - int32: totalChunks - Total number of chunks
  - int32: width - Image width in pixels
  - int32: height - Image height in pixels
  - blob: PNG data - Chunk of PNG file
```

**Client must send after each chunk:**
```
Path: /ack_chunk
Arguments:
  - int32: requestId
  - int32: chunkNum
```

**Process:**
1. Server sends chunk 0
2. Client receives chunk 0
3. Client sends `/ack_chunk <requestId> 0`
4. Server sends chunk 1
5. Repeat until all chunks received
6. Client assembles chunks into complete PNG file

**Important:**
- Server waits for ACK before sending next chunk
- No ACK = transfer stalls and times out
- Chunks must be reassembled in order
- All chunks have identical width/height values

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

2. Send /get/texture/panel <pluginSlug> <moduleSlug> <scale> <requestId>
   ← Receive chunked PNG via /set/texture
   → ACK each chunk with /ack_chunk
   → Assemble PNG for static background

3. Cache this information - only needs to be done once per module type
```

### Getting Module State

```
For each module instance:

1. Send /get/module_state <moduleId>
   ← Receive /set/s/m with position and all parameter values

2. Send /get/texture/overlay <moduleId> <scale> false <requestId>
   ← Receive chunked PNG showing current knob positions, etc.

3. Optional: Send /subscribe/module/lights <moduleId>
   ← Receive /set/s/l every 30ms with light brightness values
```

### Controlling Parameters

```
When user interacts with virtual knob/slider:

1. Calculate new parameter value (within min/max from structure)

2. Send /set/param/value <moduleId> <paramId> <value>
   → Value applied in Rack immediately

3. Optional: Request new overlay render to see updated position
   Send /get/texture/overlay <moduleId> <scale> true <requestId>
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

- **Transfer stalls:** Client not sending `/ack_chunk` - ensure ACK after every received chunk
- **Timeout:** Transfer abandoned after timeout period
- **Wrong requestId in ACK:** Must match the requestId from `/set/texture`

---

## Performance Considerations

### Rendering

- **Panel textures:** Cache these - they're static per module type
- **Overlay textures:** Expensive to render - don't request every frame
- **Recommended overlay update rate:** 10-30 FPS max
- **Use subscriptions:** `/subscribe/module/lights` is more efficient than polling

### Network

- **UDP is unreliable:** Packet loss is possible - chunked protocol handles this
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

Client → Server: /get/texture/panel "Fundamental" "VCO-1" 3.0 100
Server → Client: /set/texture 100 0 3 1024 768 <PNG chunk 0>
Client → Server: /ack_chunk 100 0
Server → Client: /set/texture 100 1 3 1024 768 <PNG chunk 1>
Client → Server: /ack_chunk 100 1
Server → Client: /set/texture 100 2 3 1024 768 <PNG chunk 2>
Client → Server: /ack_chunk 100 2
(Client assembles complete PNG)
```

### Parameter Change

```
Client → Server: /set/param/value 42 0 0.75
(Parameter immediately updated in Rack)

Client → Server: /get/texture/overlay 42 2.0 true 200
Server → Client: /set/texture 200 0 1 512 384 <PNG data>
Client → Server: /ack_chunk 200 0
(Client displays updated knob position)
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
HEARTBEAT_DELAY = 1000      // Milliseconds between heartbeats
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
