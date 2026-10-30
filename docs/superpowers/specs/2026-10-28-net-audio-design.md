# bCast: streaming Rack audio to RackSim over Wi-Fi

## Goal

A simplified clone of Core's Audio-2 module that sends stereo audio from the
Rack patch to the RackSim Unreal client over the LAN, not to a sound card.
It works on its own, and works as an expander that OSCctrl manages when
placed next to it. Presets set the balance between latency and quality.

The work covers three codebases:

1. Rack plugin (`gtnosft-rack`): the bCast module, OSCctrl integration,
   and texture chunk pacing.
2. UE client (`RackSim`, UE 5.6, Win64 and Quest/Android): the receiver,
   jitter buffer, and playback.
3. Test client (`tools/bench/audio_bench.py`): measures and verifies the Rack
   side without UE.

## Decisions

| Topic | Decision |
|---|---|
| Network | LAN only. Wi-Fi is the main target, so design for burst jitter. |
| Transport | Plain UDP on its own port, not WebRTC or Pixel Streaming. |
| Codec | Opus (CELT, `OPUS_APPLICATION_RESTRICTED_LOWDELAY`), or 16-bit PCM. |
| Loss protection | Packet-level redundancy: each packet also carries the previous frames. Opus in-band FEC is SILK-only, so it doesn't help music. |
| Direction | Send only (Rack to UE). The packet format leaves room for a return path. |
| Engine timing | bCast can be the engine's master module and clock it in real time ("Master audio module" in its menu). It claims master while streaming if no audio module has it, because Rack's fallback clock runs slow and the client then underruns. |
| Clock drift | Absorbed on the client by adaptive resampling. |
| Relationship to OSCctrl | Standalone module that becomes managed when adjacent to OSCctrl. |
| Controls | Presets only. An Advanced submenu is a follow-up. |
| UE playback | Stereo, not positioned in the world. Speaker placement is a stretch goal. |
| libopus (Rack) | Rack `DEPS` / `dep.mk` with cmake. Fall back to a git submodule if CI can't build it. |
| libopus (UE) | The engine's bundled `libOpus` module (1.3.1 on Win64, 1.1 on Android). |

## Presets

All presets stream 48 kHz stereo. Rack resamples from the engine rate with
`dsp::SampleRateConverter<2>`.

| Id | Preset | Codec | Frame | Bitrate | Redundant frames | Client target (min–max) |
|---|---|---|---|---|---|---|
| 0 | Lowest latency | Opus | 5 ms (240) | 192 kbps | 0 | 20 ms (10–60) |
| 1 | Balanced (default) | Opus | 10 ms (480) | 160 kbps | 1 | 40 ms (20–120) |
| 2 | Robust | Opus | 20 ms (960) | 160 kbps | 2 | 80 ms (40–200) |
| 3 | Uncompressed | PCM s16le | 5 ms (240) | about 1.5 Mbit/s | 0 | 20 ms (10–60) |

Opus encoder settings: `OPUS_SIGNAL_MUSIC`, fullband, complexity 8,
constrained VBR, `OPUS_SET_INBAND_FEC(0)`.

The Rack side defines the preset table in `src/audio/Presets.hpp`. The client
keeps a mirror of the table, matched by id. Only the id travels over the
wire.

## Rack side

### bCast module (`src/bCast.{hpp,cpp}`)

- **Panel:** 5HP, laid out like Audio-2. Inputs L and R (R is normalled to
  L), level knob (0–2, squared gain, as in Audio-2), 6-segment VU meter per
  channel, a snapping preset knob, and a status light: off when idle, green when streaming
  standalone, blue when managed by OSCctrl. New `res/bCast.svg`.
- **Context menu:**
  - DC blocker (default on).
  - Enabled (standalone).
  - Preset. Shared by both modes and always editable; the knob, the menu
    and the client's `/set/audio/preset` all set it, and the knob follows.
  - Destination IP and port (default port 7747). These are disabled while
    managed.
  - A stats line from the client's latest `/audio/stats` (managed only).
- **Patch JSON:** `dcFilter`, `enabled`, `preset`, `host`, `port`. Managed
  enable and destination are not saved, so a module detached from OSCctrl goes back to
  its saved standalone settings.
- **Registration:** `plugin.json` gets the model, and `plugin.cpp/hpp` add
  `modelbCast`.

### Threads and data flow

```
engine thread              streamer thread                 network
process(): gain, DC,  ──►  drain ring ─► SRC to 48k ─►   UDP :7747
VU, push Frame<2>          frame accum ─► encode ─►       (DSCP EF)
into dsp::RingBuffer       redundancy ─► send
```

- **Engine thread:** `process()` pushes one frame per sample into a lock-free
  SPSC `dsp::RingBuffer<dsp::Frame<2>, 16384>` (about 340 ms at 48 kHz). It
  never blocks. When the ring is full it drops the frame and increments an
  atomic overflow counter. It pushes only while the atomic `streaming` flag
  is set.
- **Streamer thread (`src/audio/AudioStreamer.{hpp,cpp}`):** one per module.
  Each wakeup (about 1 ms) it:
  1. snapshots the config;
  2. drains the ring through the sample rate converter into a 48 kHz frame
     accumulator;
  3. for each complete frame, encodes it (`OpusEncoder`) or packs it as PCM;
  4. pushes the encoded frame into a small history for redundancy;
  5. builds a packet (see Wire protocol) and sends it on the streamer's own
     UDP socket.
- **Pacing:** packets go out as frames complete, so the send rate follows
  the engine's audio clock. If the engine stalls, nothing is sent.
- **Resets:** a change in engine sample rate, preset or destination, or
  re-enabling, resets the encoder, sample rate converter, accumulator, ring
  and history, and increments `epoch`. The sample rate change is detected in
  `onSampleRateChange`, which sets an atomic flag.
- **Config:** a mutex-protected `bCastConfig { enabled, managed, host,
  port, preset }`, with a version counter. It's written on the UI thread (by
  the menu or by OSCctrl actions) and read once per streamer wakeup. The
  engine thread never takes the lock.
- **DSCP:** `IP_TOS = 0xB8` (EF) via `setsockopt` on Linux and macOS. On
  Windows, best effort via qWAVE (`QOSCreateHandle`/`QOSAddSocketToFlow`,
  `QOSTrafficTypeVoice`). If that fails, log once and continue.
- **Socket:** a plain BSD/Winsock UDP socket (not oscpack), so it can be
  configured with `setsockopt`.
- **Teardown:** the destructor stops and joins the streamer before the ring
  buffer and encoder are destroyed.

### Expander management (OSCctrl)

OSCctrl's networking belongs to `OSCctrlWidget`, and inbound OSC already
reaches the UI thread through the widget's action queue. All management
therefore runs on the UI thread, where module pointers are stable (modules
are only deleted on the UI thread).

- **Detection:** `OSCctrlWidget::step()` checks `module->leftExpander.module`
  and `rightExpander.module` for `modelbCast`. It tracks one managed
  module id. On a change, it:
  - **attach:** sets the config to `managed = true` and `enabled = false`
    (until the client asks), then sends `/set/audio_stream`;
  - **detach:** sets `managed = false` on the old module, if it still
    exists, and restores its standalone config. Then it sends
    `/set/audio_stream` for the old id with `enabled = 0`.
- **Client lifecycle:** when the client is lost (missed heartbeats) or
  re-registers, OSCctrl sets `enabled = false` on the managed module. When
  the client registers, OSCctrl sends `/set/audio_stream` if a managed
  module exists.
- **Destination:** in managed mode, the host is OSCctrl's direct endpoint
  (the client IP) and the port is the one the client sent in
  `/set/audio/enabled`.
- **Routes:** `/set/audio/*` and `/audio/stats` run as UI actions that look
  the module up by id with `APP->engine->getModule`. If the module is
  missing or not managed by this OSCctrl, `/set/*` routes reply with
  failure, and stats are ignored.

### Texture chunk pacing (`OscSender`)

This only applies while a managed stream is enabled.

- **Token bucket:** gates the chunk queue only. Heartbeat, lights and
  priority traffic are never delayed. In `processQueue`, if the next item
  would be a chunk and the bucket lacks tokens, the worker waits on
  `queueLockCondition` with `wait_until(refillTime)`. New priority work
  still wakes it.
- **Burst:** 8 chunks (about 11.6 KB).
- **Rate control:** AIMD, driven by `/audio/stats` (every 500 ms):
  - **decrease:** if the `underruns` or `lost` delta is above 0, or
    `jitterMs` is more than 1.5× its 10 s minimum, multiply the rate by 0.7,
    with a floor of 20 Mbit/s;
  - **increase:** otherwise add 10 Mbit/s;
  - **limit:** once the rate is above 400 Mbit/s, it's unlimited;
  - **start:** unlimited when the stream starts. No stats for 2 s also
    means unlimited.
- **Interaction with back-pressure:** slower chunk sends grow
  `OscSender::backlog()`, so `ChunkedManager`'s back-pressure slows overlay
  renders on its own.
- **Bench:** `/bench/chunk_pacing <int>` toggle (with
  `BenchToggleAckBundler`), plus metrics `tx.pacing.rate_mbps`
  (gauge), `tx.pacing.wait` (sample), `audio.client.underruns`,
  `audio.client.lost` and `audio.client.jitter_ms`. `overlay_bench.py` and
  the README get these metrics, per `tools/bench/AGENTS.md`.

### Build

- `Makefile`: `DEPS += $(opus)`, a rule that downloads `opus-1.5.2.tar.gz`
  (pinned and checked with `$(SHA256)`), then runs `$(CMAKE)` with
  `-DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF
  -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_DRED=OFF -DOPUS_OSCE=OFF
  -DOPUS_FIXED_POINT=OFF` and installs into `dep/`.
  - Add `OBJECTS += dep/lib/libopus.a`, `FLAGS += -Idep/include`, and
    `SOURCES += $(wildcard src/audio/*.cpp)`.
  - Windows: link `-lqwave`.
- `build-wsl.sh` / `build-msys.sh`: make sure `make dep` runs (or is
  documented) before the build.
- `.gitignore`: `dep/`.
- CI: verify lin-x64, win-x64, mac-x64 and mac-arm64. If cmake
  cross-compiling fails on any of them, try opus's autotools build
  (`$(CONFIGURE)`). If that fails too, switch to a submodule in
  `dependencies/opus`, built by the same Makefile rule.

## Wire protocol

### Audio packets (binary UDP, Rack → client, default port 7747)

The header is 20 bytes, little-endian:

| Offset | Type | Field | Notes |
|---|---|---|---|
| 0 | u8[2] | magic | `'R','A'` |
| 2 | u8 | version | 1 |
| 3 | u8 | flags | 0 (reserved) |
| 4 | u32 | streamId | Lower 32 bits of the bCast module id |
| 8 | u16 | epoch | Incremented on every stream reset |
| 10 | u8 | codec | 0 = PCM s16le, 1 = Opus |
| 11 | u8 | channels | 2 |
| 12 | u16 | frameSamples | At 48 kHz: 240, 480 or 960 |
| 14 | u8 | frameCount | 1 + number of redundant frames |
| 15 | u8 | presetId | See Presets |
| 16 | u32 | seq | Sequence number of the newest frame |

Then `frameCount` × `{ u16 length, u8[length] }`, newest first: seq,
seq−1, …

A frame's 48 kHz timestamp is `seq × frameSamples` within an epoch. Seq starts
at 0; epoch starts at a random value per bCast instance. All presets fit in one datagram of 1232 bytes or less.

### OSC control (managed mode only)

| Direction | Address | Args |
|---|---|---|
| Rack → client | `/set/audio_stream` | `h moduleId, i streamId, i presetId, i enabled, i epoch` |
| client → Rack | `/set/audio/enabled` | `h moduleId, i enabled, i port` |
| Rack → client | `/set/audio/enabled/ack` | `h moduleId, i enabled, i success` |
| client → Rack | `/set/audio/preset` | `h moduleId, i presetId` |
| Rack → client | `/set/audio/preset/ack` | `h moduleId, i presetId, i success` |
| client → Rack | `/audio/stats` | `h moduleId, i underruns, i lost, i recovered, f jitterMs, f bufferMs` (cumulative counts) |

`API.md` documents both the binary format and these routes.

## UE client (RackSim)

### Build

`RackSim.Build.cs`: add `AudioMixer`, `Sockets` and `Networking`, and
`AddEngineThirdPartyPrivateStaticDependencies(Target, "libOpus")`.

### Components (`Source/RackSim/Comms/Audio/`)

- **`RSNetAudioProtocol.h`:** header parsing, the preset mirror, constants.
- **`FRSAudioReceiver` (`FRunnable`):**
  - Owns an `FSocket` bound to `0.0.0.0:7747`, with a 1 MB receive buffer.
    Calls `Wait(ESocketWaitConditions::WaitForRead, 50ms)`, then `RecvFrom`.
  - Validates magic, version and `streamId` (it accepts only the active
    stream id, set atomically).
  - Copies valid packets into a `TSpscQueue<FRSAudioPacket>` with the
    arrival time (`FPlatformTime::Seconds`).
  - Counts invalid and foreign packets.
  - Never touches UObjects.
- **`FRSJitterBuffer`:** plain C++, owned and called only on the audio
  render thread. `Pull(float* Out, int32 NumFrames, int32 OutRate)`:
  1. Drains the SPSC queue into a reorder buffer keyed by seq. Frames inside
     a packet expand to (seq−i, data). Duplicates and late frames are
     dropped; redundant copies fill holes.
  2. On an epoch change, flushes everything and waits until the buffer
     reaches its target again. A packet from an older epoch (16-bit serial
     order) is a straggler and is dropped, but only while the current epoch
     has had a packet in the last 250 ms; after that it's a new sender.
  3. When a frame is due:
     - if it's present, decode it (`opus_decode_float`) or convert the PCM;
     - if it's missing, use PLC (`opus_decode_float(NULL)`) for up to 3
       frames, then a 5 ms fade to silence and an underrun;
     - for PCM, repeat the last frame with a fade.
  4. **Adaptive target:** the 95th percentile of `|arrival − expected|` over
     the last 2 s, plus one frame, clamped to the preset's min and max. Each
     underrun adds one frame for 10 s.
  5. **Drift and rate:** a cubic-interpolating fractional resampler from
     48 kHz to `OutRate`. A PI controller on (buffer level − target)
     adjusts the ratio, clamped to ±0.5%.
  6. Publishes stats as atomics: underruns, lost, recovered, jitterMs,
     bufferMs.
- **`URSNetAudioComponent : USynthComponent`:** stereo, not positioned in
  the world, and not affected by attenuation. Its `OnGenerateAudio` calls
  `FRSJitterBuffer::Pull`. It owns the receiver and jitter buffer for its
  lifetime.
- **`ARSNetAudioSpeaker : AActor`:** holds the component. The game state
  spawns it. Placing virtual speakers later means moving this actor and
  enabling attenuation.

### `ACommunicationsManager` integration

- A new `/set/audio_stream` route in `UOSCReceiver` calls
  `ACommunicationsManager::HandleAudioStream`.
- `HandleAudioStream`:
  - on a new stream with `enabled = 0` (offered), spawns or starts the
    speaker, sets the active stream id, and sends
    `/set/audio/enabled 1 7747`;
  - on `enabled = 1`, makes sure it's playing;
  - on detach the module re-offers (`enabled = 0`): the client re-requests,
    Rack fails the request for the no-longer-managed module, and the failed
    ack stops the speaker (deviation 8).
- A timer on the game thread sends `/audio/stats` every 500 ms while the
  stream is active.
- `Stop()`, `OnConnectionLost` and `OnConnectionFailed` stop and destroy the
  speaker.
- Preset choice: the console variable `rs.NetAudio.Preset` (default 1)
  sends `/set/audio/preset`. A UI for it is a follow-up.
- `UOSCSender` gains `TxSetAudioEnabled`, `TxSetAudioPreset` and
  `TxAudioStats`.

### Platform notes

- Quest: measure the output latency at the default Android audio callback
  buffer (`AndroidRuntimeSettings` `AudioCallbackBufferFrameSize`). If it
  dominates, lower it to 256 frames.
- The existing `INTERNET` permission covers the new socket.
- RackSim needs OSCctrl to run at all, so it only plays streams that
  OSCctrl manages; it starts listening in response to `/set/audio_stream`.
  A standalone bCast is for the test client and other receivers.

### Tests

UE automation tests in `Source/RackSim/Tests/RSJitterBufferTest.cpp` drive
`FRSJitterBuffer` with synthetic packet timelines:

- in order (no gaps)
- reordering
- 5% loss with no redundancy (PLC; counted as lost)
- 5% loss with redundancy (recovered)
- an epoch reset (flush and buffer up again)
- ±0.3% clock drift for 60 s (the buffer level stays near the target)
- a 30 ms arrival burst at Balanced (no underrun)

## Test client (`tools/bench/audio_bench.py`)

It reuses `overlay_bench.py`'s `Client`, `connect()`, `summarize()` and JSON
result and compare code.

- **Managed (default):** connects to OSCctrl, waits for
  `/set/audio_stream`, sends `/set/audio/enabled 1 <port>`, and reports
  `/audio/stats` every 500 ms from its own simulated jitter buffer at the
  preset target.
- **`--passive`:** listens only, for a standalone bCast.
- **Metrics:**
  - arrival jitter (p50/p95/p99) and the largest gap between packets
  - loss, reordering, duplicates, and frames recovered by redundancy
  - epoch changes and effective bitrate
  - clock drift in ppm (a least-squares fit of the sample clock against
    arrival time)
  - underruns of the simulated jitter buffer
- **Options:**
  - `--overlays N`: runs the overlay stream loop in a thread at the same
    time. Results include both the audio and overlay metrics.
  - `--drop P`: drops packets at random.
  - `--preset ID`: managed mode only.
  - `--duration`, `--label`, `--json`, and `compare` (shared).
  - `--wav PATH`: decodes (Opus through optional `opuslib`) and records.
  - `--analyze`: with a sine wave patched into bCast, finds the
    frequency and counts discontinuities and dropouts in the decoded
    signal.
- `shell.nix` and `requirements.txt`: add `opuslib` (optional) and `libopus`.
- README: usage and metrics. AGENTS: the audio workflow, including always
  passing `--host 127.0.0.1` for local runs.

## Verification

1. **CI:** lin-x64, win-x64, mac-x64 and mac-arm64 all build with libopus.
2. **Local (WSL):** run `audio_bench --passive --host 127.0.0.1` against a
   standalone bCast with every preset. Expect 0 loss and no glitches from
   `--analyze`. Then do the same in managed mode with OSCctrl.
3. **Wi-Fi:** Rack on Windows, bench on a second machine. Run every preset
   with `--overlays 3` and `--overlays 16`, A/B on `/bench/chunk_pacing`.
   - **Target:** with pacing on, Balanced has 0 underruns at 16 overlays,
     and total overlay fps is within 15% of pacing off.
4. **UE:** jitter buffer automation tests pass. On Quest standalone over
   Wi-Fi, playback is clean with overlays open. Measure end-to-end latency
   with a click track for each preset.
5. **Results:** go into a new "Audio" section in `tools/bench/PERFLOG.md`.

## Follow-ups (out of scope)

- Advanced settings: frame size, bitrate, redundancy, jitter target.
- A client UI for enabling the stream and choosing a preset.
- Placing virtual speakers in UE (positioned, with attenuation).
- Mixing several bCast streams on the client.
- A return path (UE → Rack audio).
- Audio/visual sync timestamps for overlays.
