# bCast network audio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stream stereo audio from a new Rack module, bCast, to the RackSim
Unreal client over LAN Wi-Fi. bCast works standalone and as an OSCctrl
expander. The plan also adds a Python test client that verifies the Rack side
without UE.

**Architecture:**

- **Engine thread:** bCast's `process()` pushes frames into a lock-free ring.
- **Streamer thread:** a per-module thread resamples to 48 kHz, encodes each
  frame (Opus or PCM), adds redundant frames, and sends binary UDP packets to
  port 7747 with DSCP EF.
- **OSCctrl:** when bCast sits next to OSCctrl, OSCctrl manages it over OSC
  and paces texture chunk sends based on the client's audio stats.
- **RackSim:** receives packets on its own thread, then plays them through an
  adaptive jitter buffer in a `USynthComponent`.

**Tech Stack:**

- Rack: C++20, the VCV Rack SDK (`dsp::RingBuffer`, `dsp::SampleRateConverter`),
  and libopus 1.5.2 (static, built through `dep.mk`).
- Sockets: BSD sockets / Winsock with qWAVE.
- Test client: Python 3 with python-osc, and opuslib (optional).
- UE: 5.6 with the bundled libOpus (1.3.1 on Win64, 1.1 on Android; both
  decode the plugin's stream), `USynthComponent`, `FSocket`, and automation
  tests.

**Spec:** `docs/superpowers/specs/2026-10-28-net-audio-design.md`

---

## Global Constraints

- **Rack-side naming:**
  - Rack-side names use **bCast**: module slug `bCast`, model `modelbCast`,
    C++ namespace `bcast`, files in `src/audio/`.
  - The UE side keeps **NetAudio**: `RSNetAudio*` and the `RackSim.NetAudio.*`
    tests.
- **Process:**
  - Never commit. Each task ends with the user reviewing and committing.
  - Build Rack from WSL with `./build-wsl.sh` (add `BENCH=1` for bench
    builds), never plain `make`. The user builds Windows with
    `./build-msys.sh` and builds UE themselves.
- **Audio format:** 48 kHz, 2 channels.
- **Presets:** these exact values must match on Rack, Python and UE:

  | id | name | codec | frameSamples | bitrate | redundancy | target/min/max ms |
  |---|---|---|---|---|---|---|
  | 0 | Lowest latency | Opus | 240 | 192000 | 0 | 20/10/60 |
  | 1 | Balanced (default) | Opus | 480 | 160000 | 1 | 40/20/120 |
  | 2 | Robust | Opus | 960 | 160000 | 2 | 80/40/200 |
  | 3 | Uncompressed | PCM s16le | 240 | — | 0 | 20/10/60 |

- **Opus encoder:** `OPUS_APPLICATION_RESTRICTED_LOWDELAY`, `OPUS_SIGNAL_MUSIC`,
  `OPUS_BANDWIDTH_FULLBAND`, complexity 8, VBR with
  `OPUS_SET_VBR_CONSTRAINT(1)`, and `OPUS_SET_INBAND_FEC(0)`.
- **Packet:** a 20-byte little-endian header, laid out with Python struct
  format `<2sBBIHBBHBBI`:
  - magic `'R','A'`
  - version `1`
  - flags `0`
  - streamId (u32, the lower 32 bits of the module id)
  - epoch (u16)
  - codec (u8: `0` = PCM, `1` = Opus)
  - channels `2`
  - frameSamples (u16)
  - frameCount (u8)
  - presetId (u8)
  - seq (u32, the newest frame)

  Then `frameCount × {u16 len, bytes}`, newest first. A datagram is at most
  1232 bytes, and the default port is 7747.
- **OSC routes:**

  | Direction | Address | Arguments |
  |---|---|---|
  | Rack → client | `/set/audio_stream` | `h moduleId, i streamId, i presetId, i enabled, i epoch` |
  | client → Rack | `/set/audio/enabled` | `h moduleId, i enabled, i port` |
  | Rack → client | `/set/audio/enabled/ack` | `h moduleId, i enabled, i success` |
  | client → Rack | `/set/audio/preset` | `h moduleId, i presetId` |
  | Rack → client | `/set/audio/preset/ack` | `h moduleId, i presetId, i success` |
  | client → Rack | `/audio/stats` | `h moduleId, i underruns, i lost, i recovered, f jitterMs, f bufferMs` |

  The `/audio/stats` counts are cumulative.
- **Pacing:**
  - Token bucket: burst `8 × 1452` bytes. AIMD multiplies the rate by 0.7
    (floor 20 Mbit/s) or adds 10 Mbit/s. Reaching 400 Mbit/s makes it
    unlimited, and the first decrease from unlimited starts from 400. It is also unlimited at stream start and after 2 s with no
    stats.
  - Stats arrive every 500 ms.
  - Toggle: `/bench/chunk_pacing`.
  - Metrics: `tx.pacing.rate_mbps`, `tx.pacing.wait`, `audio.client.underruns`,
    `audio.client.lost` and `audio.client.jitter_ms`.

## Deviations from the spec and assumptions

The spec's intent is unchanged in each case.

1. **Native unit-test harness (`tests/`, `tests/run.sh`):** the repo has no
   C++ tests. The packet writer, encoder and pacer don't depend on Rack, and
   TDD needs a runner. The harness is about 40 lines and is never part of the
   plugin build.
2. **Pacer jitter trouble:** jitter above `max(1.5 × 10 s minimum, minimum +
   2 ms)`. Without the 2 ms floor, a quiet link with 0.4 ms jitter would back
   off on 0.6 ms of noise.
3. **qWAVE traffic type:** `QOSTrafficTypeVoice`, not `AudioVideo`. Voice maps
   to WMM AC_VO, which is the priority we want.
4. **Speaker ownership:** `ACommunicationsManager` spawns and owns
   `ARSNetAudioSpeaker`, not the game state. It already owns every other
   connection-scoped object and the stop/lost paths.
5. **Per-environment dependency dirs:**
   - `build-wsl.sh` exports `DEP_LOCAL=dep-wsl` and `build-msys.sh`
     `DEP_LOCAL=dep-msys`, because WSL and MSYS share the checkout and each
     needs its own libopus.
   - CI keeps the default `dep`.
   - libopus builds as an ordinary prerequisite of the plugin, so `make`
     alone is enough. `make dep` also works.
6. **Client reaction to a failed `/set/audio/enabled/ack`:** RackSim stops its
   speaker. The spec doesn't say what happens; this avoids the client
   listening forever.
7. **Adaptive target floor:** the jitter buffer's target never drops below
   the preset's target: `clamp(max(preset target, p95 jitter + 1 frame) +
   boost, min, max)`. The spec's pure `p95 + 1 frame` would shrink Balanced
   to about 20 ms on a quiet link and fail the spec's own "30 ms burst, no
   underrun" test. `MinMs` stays as a clamp for a later Advanced menu.
8. **Every `enabled = 0` announce is an offer:** RackSim answers each
   `/set/audio_stream ... enabled 0` by stopping and requesting that
   stream, instead of the spec's "detach stops the speaker". On detach the
   request fails, and the failed ack (deviation 6) stops playback. The
   spec's rule needs to tell a detach from a re-registration offer, which
   races with register retries.
9. **Jitter buffer resync on a sender restart:** a packet far *behind* the
   next due frame, while the buffer is starved, restarts it like one far
   ahead. A reloaded patch keeps the module id, and the epoch starts at 0
   again, so without this the buffer would drop every packet as already
   played.

## File map

**Rack (`gtnosft-rack`):**

| File | Status | Responsibility |
|---|---|---|
| `Makefile` | modify | libopus dep rule, audio sources, `-lqwave` |
| `build-wsl.sh`, `build-msys.sh` | modify | export `DEP_LOCAL` |
| `.gitignore` | modify | dep dirs, `build-tests/` |
| `tests/test.hpp`, `tests/main.cpp`, `tests/run.sh` | create | native test harness |
| `tests/test_packet.cpp`, `tests/test_encoder.cpp`, `tests/test_pacer.cpp` | create | unit tests |
| `src/audio/Presets.hpp` | create | preset table |
| `src/audio/Packet.{hpp,cpp}` | create | wire header and `writePacket` |
| `src/audio/AudioEncoder.{hpp,cpp}` | create | Opus/PCM frame encoder |
| `src/audio/UdpAudioSocket.{hpp,cpp}` | create | UDP socket with DSCP/qWAVE |
| `src/audio/AudioStreamer.{hpp,cpp}` | create | streamer thread |
| `src/audio/bCast.{hpp,cpp}` | create | module, widget, menu, config API |
| `res/bCast.svg` | create | panel |
| `src/plugin.{hpp,cpp}`, `plugin.json` | modify | registration |
| `src/osc/ChunkPacer.{hpp,cpp}` | create | token bucket and AIMD |
| `src/osc/OscSender.{hpp,cpp}` | modify | pacer in `processQueue`, `directHost()` |
| `src/osc/Bundler/AudioBundlers.{hpp,cpp}` | create | `/set/audio_stream` and acks |
| `src/osc/OscReceiver.cpp` | modify | audio routes, bench toggle, client lifecycle hooks |
| `src/OSCctrl.{hpp,cpp}` | modify | expander detection and management |
| `API.md` | modify | binary format and routes |
| `tools/bench/audio_analysis.py`, `test_audio_analysis.py` | create | pure analysis and tests |
| `tools/bench/audio_bench.py` | create | test client |
| `tools/bench/mock_server.py` | modify | `--audio-to`, `--audio`, pacing toggle |
| `tools/bench/overlay_bench.py` | modify | `--no-chunk-pacing`, pacing metrics |
| `tools/bench/README.md`, `AGENTS.md`, `PERFLOG.md`, `requirements.txt`, `shell.nix` | modify | docs and deps |

**UE (`/home/ont/dev/RackSim/Source/RackSim`):**

| File | Status | Responsibility |
|---|---|---|
| `RackSim.Build.cs` | modify | modules and libOpus |
| `Comms/Audio/RSNetAudioProtocol.{h,cpp}` | create | parse, preset mirror |
| `Comms/Audio/RSJitterBuffer.{h,cpp}` | create | reorder, decode, PLC, adaptive target, resample |
| `Comms/Audio/RSAudioReceiver.{h,cpp}` | create | socket thread |
| `Comms/Audio/RSNetAudioComponent.{h,cpp}` | create | `USynthComponent` |
| `Comms/Audio/RSNetAudioSpeaker.{h,cpp}` | create | actor |
| `Comms/OSCReceiver.{h,cpp}`, `Comms/OSCSender.{h,cpp}`, `Comms/CommunicationsManager.{h,cpp}` | modify | routes, Tx, lifecycle, stats timer, console variable |
| `Tests/RSNetAudioProtocolTest.cpp`, `Tests/RSJitterBufferTest.cpp` | create | automation tests |

## Task order and dependencies

```
1 deps+harness ─► 2 packet ─► 3 encoder ─┐
4 py analysis ─► 5 py client             ├─► 6 module ─► 7 streamer ─► 9 OSCctrl ─► 14 verify
8 pacer ──────────────────────────────────────────────────────────────┘                ▲
10 UE protocol ─► 11 jitter buffer ─► 12 receiver/component ─► 13 comms ─────────────────┘
```

Tasks 4, 5, 8 and 10–13 are independent of the Rack module work, so they can
run in parallel.

---

## Task 1: libopus dependency and native test harness

**Files:**
- Modify: `Makefile`, `build-wsl.sh`, `build-msys.sh`, `.gitignore`
- Create: `tests/test.hpp`, `tests/main.cpp`, `tests/run.sh`

**Interfaces:**
- Produces:
  - `$(DEP_LOCAL)/lib/libopus.a` and `$(DEP_LOCAL)/include/opus/opus.h`
    (include it as `<opus/opus.h>`);
  - test macros `TEST(name)`, `CHECK(cond)` and `CHECK_EQ(a, b)` (arithmetic
    values only);
  - `tests/run.sh [filter]`.

- [ ] **Step 1: Add the libopus rule and audio sources to `Makefile`**

Insert after the `BENCH` block (before `include $(RACK_DIR)/arch.mk`):

```make
# libopus, built static into $(DEP_LOCAL). build-wsl.sh / build-msys.sh set
# DEP_LOCAL per environment, since they share this checkout. CI uses `dep`.
DEP_LOCAL ?= dep
OPUS_VERSION := 1.5.2
libopus := $(DEP_LOCAL)/lib/libopus.a
OBJECTS += $(libopus)
DEPS += $(libopus)
FLAGS += -I$(DEP_LOCAL)/include
```

In the mingw branch, change `LDFLAGS += -lws2_32 -lwinmm -liphlpapi` to:

```make
	LDFLAGS += -lws2_32 -lwinmm -liphlpapi -lqwave
```

After `SOURCES += $(wildcard src/util/*.cpp)` add:

```make
SOURCES += $(wildcard src/audio/*.cpp)
```

Replace everything after `include $(RACK_DIR)/plugin.mk` with:

```make
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS))

OPUS_CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=Release \
	-DOPUS_BUILD_SHARED_LIBRARY=OFF -DOPUS_BUILD_TESTING=OFF -DOPUS_BUILD_PROGRAMS=OFF \
	-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF \
	-DOPUS_DRED=OFF -DOPUS_OSCE=OFF -DOPUS_FIXED_POINT=OFF \
	-DOPUS_STACK_PROTECTOR=OFF -DOPUS_FORTIFY_SOURCE=OFF \
	-DCMAKE_POSITION_INDEPENDENT_CODE=ON
ifdef ARCH_MAC
ifdef ARCH_ARM64
	OPUS_CMAKE_FLAGS += -DCMAKE_OSX_ARCHITECTURES=arm64
else
	OPUS_CMAKE_FLAGS += -DCMAKE_OSX_ARCHITECTURES=x86_64
endif
endif

$(libopus):
	cd $(DEP_LOCAL) && $(WGET) "https://downloads.xiph.org/releases/opus/opus-$(OPUS_VERSION).tar.gz"
	cd $(DEP_LOCAL) && $(SHA256) opus-$(OPUS_VERSION).tar.gz 65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1
	cd $(DEP_LOCAL) && $(UNTAR) opus-$(OPUS_VERSION).tar.gz
	cd $(DEP_LOCAL)/opus-$(OPUS_VERSION) && $(CMAKE) -B build $(OPUS_CMAKE_FLAGS)
	$(MAKE) -C $(DEP_LOCAL)/opus-$(OPUS_VERSION)/build
	$(MAKE) -C $(DEP_LOCAL)/opus-$(OPUS_VERSION)/build install

# audio sources include opus.h
$(filter build/src/audio/%,$(OBJECTS)): | $(libopus)

# rebuild everything when BENCH changes, since make doesn't track flags
# (but not libopus)
BENCH_STAMP := build/bench-$(BENCH).stamp
$(filter-out $(libopus),$(OBJECTS)): $(BENCH_STAMP)
$(BENCH_STAMP):
	@mkdir -p build
	@rm -f build/bench-*.stamp
	@touch $@
```

- [ ] **Step 2: Export `DEP_LOCAL` in the build wrappers**

In `build-wsl.sh`, after `readonly BUILD_NAME="build-wsl"`:

```bash
# libopus and other deps, kept apart from the MSYS build's
export DEP_LOCAL="${DEP_LOCAL:-dep-wsl}"
```

In `build-msys.sh`, after `readonly BUILD_NAME="build-msys"`:

```bash
# libopus and other deps, kept apart from the WSL build's
export DEP_LOCAL="${DEP_LOCAL:-dep-msys}"
```

- [ ] **Step 3: Ignore dependency and test output**

Append to `.gitignore`:

```
/dep/
/dep-wsl/
/dep-msys/
/build-tests/
```

- [ ] **Step 4: Build libopus**

Run: `./build-wsl.sh dep`

Expected:
- The output ends with cmake's install lines.
- `ls dep-wsl/lib/libopus.a dep-wsl/include/opus/opus.h` lists both files.

If cmake is missing in WSL, run `sudo apt install cmake` (Windows with MSYS:
`pacman -S mingw-w64-x86_64-cmake wget`).

- [ ] **Step 5: Write the harness**

`tests/test.hpp`:

```cpp
#pragma once
// Minimal test registry for the Rack-free units. Run with tests/run.sh.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {
struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
inline int& failures() {
  static int count = 0;
  return count;
}
struct Register {
  Register(const char* name, std::function<void()> fn) {
    registry().push_back({name, fn});
  }
};
}

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name) \
  static void name(); \
  static test::Register TEST_CAT(register_, name)(#name, name); \
  static void name()
#define CHECK(cond) do { \
    if (!(cond)) { \
      std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++test::failures(); \
    } \
  } while (0)
#define CHECK_EQ(a, b) do { \
    auto checkA = (a); \
    auto checkB = (b); \
    if (!(checkA == checkB)) { \
      std::printf("  %s:%d: CHECK_EQ(%s, %s) failed: %s != %s\n", __FILE__, \
        __LINE__, #a, #b, std::to_string(checkA).c_str(), \
        std::to_string(checkB).c_str()); \
      ++test::failures(); \
    } \
  } while (0)
```

`tests/main.cpp`:

```cpp
#include "test.hpp"

int main(int argc, char** argv) {
  std::string filter = argc > 1 ? argv[1] : "";
  int run = 0;
  for (auto& c : test::registry()) {
    if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos)
      continue;
    int before = test::failures();
    c.fn();
    ++run;
    std::printf("%s %s\n", test::failures() == before ? "PASS" : "FAIL", c.name);
  }
  std::printf("%d tests, %d failed checks\n", run, test::failures());
  return test::failures() ? 1 : 0;
}
```

`tests/run.sh` (then `chmod +x tests/run.sh`):

```bash
#!/bin/bash
# Builds and runs the Rack-free unit tests (packet, encoder, pacer).
# Needs libopus first: ./build-wsl.sh dep (or ./build-msys.sh dep).
# Usage: tests/run.sh [name filter]
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -z "${DEP_LOCAL:-}" ]]; then
  case "$(uname -s)" in
    MINGW*|MSYS*) DEP_LOCAL=dep-msys ;;
    *) DEP_LOCAL=dep-wsl ;;
  esac
fi
if [[ ! -f "$DEP_LOCAL/lib/libopus.a" ]]; then
  echo "missing $DEP_LOCAL/lib/libopus.a; run ./build-wsl.sh dep first" >&2
  exit 1
fi

sources=(tests/*.cpp)
for f in src/audio/Packet.cpp src/audio/AudioEncoder.cpp src/osc/ChunkPacer.cpp; do
  if [[ -f "$f" ]]; then sources+=("$f"); fi
done

mkdir -p build-tests
g++ -std=c++20 -O1 -g -Wall -Isrc -I"$DEP_LOCAL/include" \
  "${sources[@]}" "$DEP_LOCAL/lib/libopus.a" -lm -lpthread -o build-tests/unit
build-tests/unit "$@"
```

- [ ] **Step 6: Run the empty harness**

Run: `tests/run.sh`
Expected: `0 tests, 0 failed checks`, exit 0.

- [ ] **Step 7: Confirm the plugin still builds**

Run: `./build-wsl.sh -j8`
Expected:
- The build succeeds.
- `src/audio` doesn't exist yet, so nothing new compiles.
- libopus is not rebuilt, and `plugin.so` links `libopus.a`.

Check with `nm -C plugin.so | grep -c opus_encoder_create`. The count is 0
for now, because nothing references opus, so the archive members are
dropped. That's expected.

- [ ] **Step 8: Stop.** The user reviews and commits.

---

## Task 2: Presets and packet writer

**Files:**
- Create: `src/audio/Presets.hpp`, `src/audio/Packet.hpp`, `src/audio/Packet.cpp`
- Test: `tests/test_packet.cpp`

**Interfaces:**
- Produces (namespace `bcast`):
  - `enum class Codec : uint8_t { Pcm = 0, Opus = 1 }`
  - `struct Preset { int id; const char* name; Codec codec; int frameSamples; int bitrate; int redundancy; int targetMs; int minMs; int maxMs; }`
  - `PRESETS[]`, `PRESET_COUNT`, `SAMPLE_RATE`, `CHANNELS`, `DEFAULT_PRESET`
  - `bool isValidPreset(int)` and `const Preset& presetById(int)`
  - `MAGIC`, `PROTOCOL_VERSION`, `HEADER_SIZE`, `MAX_DATAGRAM`, `DEFAULT_PORT`
  - `struct PacketHeader { uint32_t streamId; uint16_t epoch; Codec codec; uint16_t frameSamples; uint8_t presetId; uint32_t seq; }`
  - `using EncodedFrame = std::vector<uint8_t>`
  - `size_t maxFrameBytes(int frameCount)`
  - `size_t writePacket(uint8_t* out, size_t cap, const PacketHeader&, const std::vector<const EncodedFrame*>& frames)`.
    Frames are newest first. It returns the packet size, or 0 if the frames
    don't fit.

- [ ] **Step 1: Write the failing tests**

`tests/test_packet.cpp`:

```cpp
#include "test.hpp"
#include "audio/Packet.hpp"

using namespace bcast;

TEST(packet_header_layout) {
  EncodedFrame newest{0xAA, 0xBB, 0xCC};
  EncodedFrame older{0x11};
  PacketHeader h{0x04030201u, 0x0605, Codec::Opus, 480, 1, 0x0A090807u};
  uint8_t buf[MAX_DATAGRAM];

  size_t n = writePacket(buf, sizeof(buf), h, {&newest, &older});

  CHECK_EQ(n, HEADER_SIZE + 2 + 3 + 2 + 1);
  const uint8_t expected[20] = {
    'R', 'A', 1, 0,
    0x01, 0x02, 0x03, 0x04,  // streamId
    0x05, 0x06,              // epoch
    1, 2,                    // codec, channels
    0xE0, 0x01,              // frameSamples 480
    2, 1,                    // frameCount, presetId
    0x07, 0x08, 0x09, 0x0A,  // seq
  };
  for (int i = 0; i < 20; ++i) CHECK_EQ(buf[i], expected[i]);
  CHECK_EQ(buf[20], 3);
  CHECK_EQ(buf[21], 0);
  CHECK_EQ(buf[22], 0xAA);
  CHECK_EQ(buf[24], 0xCC);
  CHECK_EQ(buf[25], 1);
  CHECK_EQ(buf[26], 0);
  CHECK_EQ(buf[27], 0x11);
}

TEST(packet_rejects_oversize) {
  EncodedFrame big(MAX_DATAGRAM, 0);
  PacketHeader h{1, 0, Codec::Pcm, 240, 3, 0};
  uint8_t buf[MAX_DATAGRAM];
  CHECK_EQ(writePacket(buf, sizeof(buf), h, {&big}), (size_t)0);
  CHECK_EQ(writePacket(buf, sizeof(buf), h, {}), (size_t)0);
}

TEST(packet_max_frame_bytes_fill_datagram) {
  for (int count = 1; count <= 3; ++count) {
    std::vector<EncodedFrame> frames(count, EncodedFrame(maxFrameBytes(count), 7));
    std::vector<const EncodedFrame*> ptrs;
    for (auto& f : frames) ptrs.push_back(&f);
    PacketHeader h{1, 0, Codec::Opus, 960, 2, 5};
    uint8_t buf[MAX_DATAGRAM];
    size_t n = writePacket(buf, sizeof(buf), h, ptrs);
    CHECK(n > MAX_DATAGRAM - 3);
    CHECK(n <= MAX_DATAGRAM);
  }
  CHECK_EQ(maxFrameBytes(3), (size_t)402);
}

TEST(presets_fallback_to_default) {
  CHECK_EQ(presetById(-1).id, DEFAULT_PRESET);
  CHECK_EQ(presetById(PRESET_COUNT).id, DEFAULT_PRESET);
  CHECK_EQ(presetById(3).id, 3);
  // PCM 5 ms stereo fits a single datagram
  CHECK(presetById(3).frameSamples * 4 <= (int)maxFrameBytes(1));
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `tests/run.sh packet`
Expected: the compile fails with `audio/Packet.hpp: No such file or directory`.

- [ ] **Step 3: Implement**

`src/audio/Presets.hpp`:

```cpp
#pragma once
#include <cstdint>

// Mirrored by tools/bench/audio_analysis.py and RackSim's
// RSNetAudioProtocol.h. Only the id travels over the wire.
namespace bcast {

enum class Codec : uint8_t { Pcm = 0, Opus = 1 };

struct Preset {
  int id;
  const char* name;
  Codec codec;
  int frameSamples; // per channel, at 48 kHz
  int bitrate;      // Opus only
  int redundancy;   // previous frames repeated in each packet
  int targetMs;     // client jitter buffer target
  int minMs;
  int maxMs;
};

constexpr int SAMPLE_RATE = 48000;
constexpr int CHANNELS = 2;
constexpr int DEFAULT_PRESET = 1;

constexpr Preset PRESETS[] = {
  {0, "Lowest latency", Codec::Opus, 240, 192000, 0, 20, 10, 60},
  {1, "Balanced", Codec::Opus, 480, 160000, 1, 40, 20, 120},
  {2, "Robust", Codec::Opus, 960, 160000, 2, 80, 40, 200},
  {3, "Uncompressed", Codec::Pcm, 240, 0, 0, 20, 10, 60},
};
constexpr int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

constexpr bool isValidPreset(int id) { return id >= 0 && id < PRESET_COUNT; }
constexpr const Preset& presetById(int id) {
  return PRESETS[isValidPreset(id) ? id : DEFAULT_PRESET];
}

}
```

`src/audio/Packet.hpp`:

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Presets.hpp"

// Binary audio packet. See API.md "Audio stream".
namespace bcast {

constexpr uint8_t MAGIC[2] = {'R', 'A'};
constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr size_t HEADER_SIZE = 20;
constexpr size_t MAX_DATAGRAM = 1232;
constexpr int DEFAULT_PORT = 7747;

struct PacketHeader {
  uint32_t streamId;
  uint16_t epoch;
  Codec codec;
  uint16_t frameSamples;
  uint8_t presetId;
  uint32_t seq; // newest frame
};

using EncodedFrame = std::vector<uint8_t>;

// largest encoded frame that still lets frameCount of them fit one datagram
constexpr size_t maxFrameBytes(int frameCount) {
  return (MAX_DATAGRAM - HEADER_SIZE) / frameCount - 2;
}

// frames newest first. returns the packet size, or 0 if they don't fit
size_t writePacket(
  uint8_t* out,
  size_t cap,
  const PacketHeader& header,
  const std::vector<const EncodedFrame*>& frames
);

}
```

`src/audio/Packet.cpp`:

```cpp
#include "Packet.hpp"

#include <cstring>

namespace bcast {

namespace {
void put16(uint8_t* p, uint16_t v) {
  p[0] = v & 0xff;
  p[1] = v >> 8;
}
void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = (v >> (8 * i)) & 0xff;
}
}

size_t writePacket(
  uint8_t* out,
  size_t cap,
  const PacketHeader& header,
  const std::vector<const EncodedFrame*>& frames
) {
  if (frames.empty() || frames.size() > 255) return 0;
  size_t size = HEADER_SIZE;
  for (const EncodedFrame* f : frames) {
    if (f->size() > 0xffff) return 0;
    size += 2 + f->size();
  }
  if (size > cap) return 0;

  out[0] = MAGIC[0];
  out[1] = MAGIC[1];
  out[2] = PROTOCOL_VERSION;
  out[3] = 0;
  put32(out + 4, header.streamId);
  put16(out + 8, header.epoch);
  out[10] = (uint8_t)header.codec;
  out[11] = CHANNELS;
  put16(out + 12, header.frameSamples);
  out[14] = (uint8_t)frames.size();
  out[15] = header.presetId;
  put32(out + 16, header.seq);

  uint8_t* p = out + HEADER_SIZE;
  for (const EncodedFrame* f : frames) {
    put16(p, (uint16_t)f->size());
    p += 2;
    std::memcpy(p, f->data(), f->size());
    p += f->size();
  }
  return size;
}

}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `tests/run.sh`
Expected: four `PASS` lines and `4 tests, 0 failed checks`.

- [ ] **Step 5: Stop.** The user reviews and commits.

---

## Task 3: AudioEncoder

**Files:**
- Create: `src/audio/AudioEncoder.hpp`, `src/audio/AudioEncoder.cpp`
- Test: `tests/test_encoder.cpp`

**Interfaces:**
- Consumes: `Preset` and `maxFrameBytes` (from Task 2).
- Produces `bcast::AudioEncoder` (not copyable):
  - `bool reset(const Preset&)` returns false if Opus init fails.
  - `int encode(const float* in, uint8_t* out, int cap)`:
    - `in` holds `frameSamples × 2` interleaved floats in [-1, 1];
    - returns the number of bytes, or -1.

- [ ] **Step 1: Write the failing tests**

`tests/test_encoder.cpp`:

```cpp
#include "test.hpp"
#include "audio/AudioEncoder.hpp"
#include "audio/Packet.hpp"

#include <cmath>
#include <opus/opus.h>

using namespace bcast;

static std::vector<float> sine(int frames, int offset, float amp = 0.5f) {
  std::vector<float> out(frames * 2);
  for (int i = 0; i < frames; ++i) {
    float v = amp * std::sin(2.f * (float)M_PI * 1000.f * (i + offset) / SAMPLE_RATE);
    out[2 * i] = v;
    out[2 * i + 1] = v;
  }
  return out;
}

TEST(encoder_pcm_s16le) {
  AudioEncoder enc;
  CHECK(enc.reset(PRESETS[3]));
  std::vector<float> in(240 * 2, 0.f);
  in[0] = 1.f;
  in[1] = -1.f;
  in[2] = 1.5f;  // clamps
  in[3] = 0.5f;
  uint8_t out[2000];
  int n = enc.encode(in.data(), out, sizeof(out));
  CHECK_EQ(n, 240 * 4);
  CHECK_EQ(out[0], 0xFF); CHECK_EQ(out[1], 0x7F);  // 32767
  CHECK_EQ(out[2], 0x01); CHECK_EQ(out[3], 0x80);  // -32767
  CHECK_EQ(out[4], 0xFF); CHECK_EQ(out[5], 0x7F);
  CHECK_EQ(out[6], 0x00); CHECK_EQ(out[7], 0x40);  // lrint(16383.5) = 16384
}

TEST(encoder_pcm_rejects_small_buffer) {
  AudioEncoder enc;
  enc.reset(PRESETS[3]);
  std::vector<float> in(240 * 2, 0.f);
  uint8_t out[100];
  CHECK_EQ(enc.encode(in.data(), out, sizeof(out)), -1);
}

TEST(encoder_opus_fits_packet_and_round_trips) {
  for (int id = 0; id <= 2; ++id) {
    const Preset& p = PRESETS[id];
    AudioEncoder enc;
    CHECK(enc.reset(p));
    int err = 0;
    OpusDecoder* dec = opus_decoder_create(SAMPLE_RATE, CHANNELS, &err);
    CHECK_EQ(err, OPUS_OK);

    int cap = (int)maxFrameBytes(1 + p.redundancy);
    std::vector<uint8_t> out(cap);
    std::vector<float> decoded(p.frameSamples * 2);
    double rms = 0;
    for (int f = 0; f < 50; ++f) {
      std::vector<float> in = sine(p.frameSamples, f * p.frameSamples);
      int n = enc.encode(in.data(), out.data(), cap);
      CHECK(n > 0);
      CHECK(n <= cap);
      int got = opus_decode_float(dec, out.data(), n, decoded.data(), p.frameSamples, 0);
      CHECK_EQ(got, p.frameSamples);
      if (f == 49) {
        for (float v : decoded) rms += v * v;
        rms = std::sqrt(rms / decoded.size());
      }
    }
    // 0.5 amplitude sine: rms 0.354
    CHECK(std::fabs(rms - 0.354) < 0.05);
    opus_decoder_destroy(dec);
  }
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `tests/run.sh encoder`
Expected: the compile fails with `audio/AudioEncoder.hpp: No such file or directory`.

- [ ] **Step 3: Implement**

`src/audio/AudioEncoder.hpp`:

```cpp
#pragma once
#include <cstdint>

#include "Presets.hpp"

struct OpusEncoder;

namespace bcast {

// Encodes one frame of 48 kHz interleaved stereo as Opus or s16le PCM
struct AudioEncoder {
  AudioEncoder() = default;
  ~AudioEncoder();
  AudioEncoder(const AudioEncoder&) = delete;
  AudioEncoder& operator=(const AudioEncoder&) = delete;

  // false if the Opus encoder couldn't be created
  bool reset(const Preset& preset);
  // in: preset.frameSamples * 2 floats. returns bytes written, or -1
  int encode(const float* in, uint8_t* out, int cap);

private:
  OpusEncoder* opus = nullptr;
  Preset preset = PRESETS[DEFAULT_PRESET];
  void destroy();
};

}
```

`src/audio/AudioEncoder.cpp`:

```cpp
#include "AudioEncoder.hpp"

#include <algorithm>
#include <cmath>
#include <opus/opus.h>

namespace bcast {

AudioEncoder::~AudioEncoder() { destroy(); }

void AudioEncoder::destroy() {
  if (opus) opus_encoder_destroy(opus);
  opus = nullptr;
}

bool AudioEncoder::reset(const Preset& p) {
  destroy();
  preset = p;
  if (p.codec == Codec::Pcm) return true;

  int err = 0;
  opus = opus_encoder_create(
    SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err
  );
  if (err != OPUS_OK || !opus) {
    opus = nullptr;
    return false;
  }
  opus_encoder_ctl(opus, OPUS_SET_BITRATE(p.bitrate));
  opus_encoder_ctl(opus, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
  opus_encoder_ctl(opus, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_FULLBAND));
  opus_encoder_ctl(opus, OPUS_SET_COMPLEXITY(8));
  opus_encoder_ctl(opus, OPUS_SET_VBR(1));
  opus_encoder_ctl(opus, OPUS_SET_VBR_CONSTRAINT(1));
  // in-band FEC is SILK-only; redundancy is packet-level instead
  opus_encoder_ctl(opus, OPUS_SET_INBAND_FEC(0));
  return true;
}

int AudioEncoder::encode(const float* in, uint8_t* out, int cap) {
  int samples = preset.frameSamples * CHANNELS;
  if (preset.codec == Codec::Pcm) {
    if (cap < samples * 2) return -1;
    for (int i = 0; i < samples; ++i) {
      float v = std::clamp(in[i], -1.f, 1.f);
      int16_t s = (int16_t)std::lrint(v * 32767.f);
      out[2 * i] = (uint8_t)(s & 0xff);
      out[2 * i + 1] = (uint8_t)((uint16_t)s >> 8);
    }
    return samples * 2;
  }
  if (!opus) return -1;
  int n = opus_encode_float(opus, in, preset.frameSamples, out, cap);
  return n < 0 ? -1 : n;
}

}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `tests/run.sh`
Expected: 7 `PASS` lines and `7 tests, 0 failed checks`.

- [ ] **Step 5: Stop.** The user reviews and commits.

---
## Task 4: Python audio analysis library

**Files:**
- Create: `tools/bench/audio_analysis.py`
- Test: `tools/bench/test_audio_analysis.py`

**Interfaces:**
- Produces (pure, no sockets):
  - constants `HEADER`, `PRESETS` (list of `Preset`), `CODEC_PCM`, `CODEC_OPUS`
  - `parse_packet(data) -> Packet` (raises `ValueError`); `Packet.frames` is
    `[(seq, payload)]`, newest first
  - `build_packet(stream_id, epoch, codec, frame_samples, preset_id, seq, payloads)`
  - `StreamTracker.feed(packet, arrival, size)`, `.close()`, `.summary()`
  - `JitterSim(target_ms).feed(packet, arrival)` with attributes `underruns`,
    `lost` and `recovered`
  - `decode_stream(codec, frame_samples, payloads_by_seq, first, last) -> list[float]`
    (interleaved)
  - `analyze_sine(mono, sample_rate) -> dict`

- [ ] **Step 1: Write the failing tests**

`tools/bench/test_audio_analysis.py`:

```python
"""Run: .venv/bin/python -m unittest test_audio_analysis -v (from tools/bench)"""

import math
import struct
import unittest

import audio_analysis as aa


def pcm_payload(values):
    return struct.pack(f"<{len(values)}h", *values)


def packet(seq, epoch=0, redundancy=0, frame_samples=480, codec=aa.CODEC_OPUS, preset=1):
    payloads = [bytes([s & 0xFF]) for s in range(seq, max(-1, seq - 1 - redundancy), -1)]
    return aa.parse_packet(aa.build_packet(4242, epoch, codec, frame_samples, preset, seq, payloads))


class PacketTests(unittest.TestCase):
    def test_round_trip(self):
        data = aa.build_packet(7, 3, aa.CODEC_OPUS, 480, 1, 10, [b"new", b"old"])
        self.assertEqual(len(data), 20 + 2 + 3 + 2 + 3)
        p = aa.parse_packet(data)
        self.assertEqual((p.stream_id, p.epoch, p.codec, p.channels), (7, 3, 1, 2))
        self.assertEqual((p.frame_samples, p.preset_id, p.seq), (480, 1, 10))
        self.assertEqual(p.frames, [(10, b"new"), (9, b"old")])

    def test_matches_rack_layout(self):
        # same bytes as tests/test_packet.cpp packet_header_layout
        data = aa.build_packet(0x04030201, 0x0605, 1, 480, 1, 0x0A090807, [b"\xaa\xbb\xcc", b"\x11"])
        self.assertEqual(
            data[:20],
            bytes([82, 65, 1, 0, 1, 2, 3, 4, 5, 6, 1, 2, 0xE0, 1, 2, 1, 7, 8, 9, 10]),
        )

    def test_rejects_bad_packets(self):
        good = aa.build_packet(7, 0, 1, 480, 1, 0, [b"abc"])
        with self.assertRaises(ValueError):
            aa.parse_packet(b"XX" + good[2:])
        with self.assertRaises(ValueError):
            aa.parse_packet(good[:-1])
        with self.assertRaises(ValueError):
            aa.parse_packet(good[:10])


class TrackerTests(unittest.TestCase):
    def test_loss_recovery_duplicates_reorder(self):
        t = aa.StreamTracker()
        fs = 0.01
        # seq 3 missing entirely, seq 5 only arrives as a redundant copy in 6,
        # seq 7 duplicated, seq 9 arrives before 8
        order = [0, 1, 2, 4, 6, 7, 7, 9, 8, 10]
        for i, s in enumerate(order):
            t.feed(packet(s, redundancy=1), i * fs, 100)
        t.close()
        s = t.summary()
        self.assertEqual(s["duplicates"], 1)
        self.assertEqual(s["reordered"], 1)
        # 3 is covered by 4's redundant copy, 5 by 6's
        self.assertEqual(s["recovered"], 2)
        self.assertEqual(s["lost"], 0)
        self.assertEqual(s["frames_expected"], 11)

    def test_loss_without_redundancy(self):
        t = aa.StreamTracker()
        for s in (0, 1, 3, 4):
            t.feed(packet(s), s * 0.01, 50)
        t.close()
        self.assertEqual(t.summary()["lost"], 1)

    def test_epoch_change_closes_previous(self):
        t = aa.StreamTracker()
        for s in (0, 2):
            t.feed(packet(s, epoch=0), s * 0.01, 50)
        for s in (0, 1):
            t.feed(packet(s, epoch=1), 0.1 + s * 0.01, 50)
        t.close()
        s = t.summary()
        self.assertEqual(s["epoch_changes"], 1)
        self.assertEqual(s["lost"], 1)

    def test_jitter_and_drift(self):
        t = aa.StreamTracker()
        fs = 0.01
        # sender clock 100 ppm fast: media time runs ahead of arrival time
        for s in range(500):
            arrival = s * fs / 1.0001 + (0.004 if s % 50 == 0 else 0.0)
            t.feed(packet(s), arrival, 50)
        t.close()
        s = t.summary()
        self.assertAlmostEqual(s["drift_ppm"], 100, delta=10)
        self.assertGreater(s["jitter_ms"]["max"], 3.5)
        self.assertLess(s["jitter_ms"]["p50"], 1.0)


class JitterSimTests(unittest.TestCase):
    def test_steady_stream_no_underrun(self):
        sim = aa.JitterSim(40)
        for s in range(200):
            sim.feed(packet(s), s * 0.01 + (0.02 if s % 20 == 0 else 0))
        self.assertEqual((sim.underruns, sim.lost), (0, 0))

    def test_burst_longer_than_target_underruns(self):
        sim = aa.JitterSim(20)
        t = 0.0
        for s in range(100):
            t = s * 0.01 + (0.06 if 50 <= s < 53 else 0)
            sim.feed(packet(s), t)
        self.assertGreaterEqual(sim.underruns, 1)

    def test_redundant_copy_counts_as_recovered(self):
        sim = aa.JitterSim(40)
        for s in range(20):
            if s == 10:
                continue
            sim.feed(packet(s, redundancy=1), s * 0.01)
        self.assertEqual((sim.recovered, sim.lost, sim.underruns), (1, 0, 0))

    def test_missing_frame_with_later_frames_is_lost(self):
        sim = aa.JitterSim(40)
        for s in range(20):
            if s == 10:
                continue
            sim.feed(packet(s), s * 0.01)
        self.assertEqual((sim.lost, sim.underruns), (1, 0))


class SignalTests(unittest.TestCase):
    def sine(self, n, f=1000, amp=0.5, sr=48000):
        return [amp * math.sin(2 * math.pi * f * i / sr) for i in range(n)]

    def test_clean_sine(self):
        r = aa.analyze_sine(self.sine(48000), 48000)
        self.assertAlmostEqual(r["frequency_hz"], 1000, delta=1)
        self.assertEqual((r["glitches"], r["dropouts"]), (0, 0))

    def test_discontinuity(self):
        x = self.sine(48000)
        x = x[:20000] + x[20100:]  # phase jump
        self.assertEqual(aa.analyze_sine(x, 48000)["glitches"], 1)

    def test_dropout(self):
        x = self.sine(48000)
        x[10000:10480] = [0.0] * 480
        r = aa.analyze_sine(x, 48000)
        self.assertEqual(r["dropouts"], 1)

    def test_decode_pcm_fills_missing_with_silence(self):
        a = pcm_payload([32767, -32767, 0, 0])
        out = aa.decode_stream(aa.CODEC_PCM, 2, {0: a, 2: a}, 0, 2)
        self.assertEqual(len(out), 12)
        self.assertAlmostEqual(out[0], 1.0, places=3)
        self.assertEqual(out[4:8], [0.0] * 4)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `cd tools/bench && .venv/bin/python -m unittest test_audio_analysis -v`
Expected: `ModuleNotFoundError: No module named 'audio_analysis'`.

(If `.venv` is missing, follow `tools/bench/README.md` section 2 to create it.)

- [ ] **Step 3: Implement `tools/bench/audio_analysis.py`**

```python
"""Pure helpers for bCast audio streams: packet parsing, arrival statistics,
a simulated jitter buffer and sine checks. No sockets; audio_bench.py does
the I/O.

Mirrors src/audio/Packet.hpp and src/audio/Presets.hpp.
"""

import math
import struct
from dataclasses import dataclass

MAGIC = b"RA"
VERSION = 1
# magic, version, flags, streamId, epoch, codec, channels, frameSamples,
# frameCount, presetId, seq
HEADER = struct.Struct("<2sBBIHBBHBBI")
SAMPLE_RATE = 48000
CHANNELS = 2
CODEC_PCM, CODEC_OPUS = 0, 1


@dataclass(frozen=True)
class Preset:
    id: int
    name: str
    codec: int
    frame_samples: int
    bitrate: int
    redundancy: int
    target_ms: int
    min_ms: int
    max_ms: int


PRESETS = [
    Preset(0, "Lowest latency", CODEC_OPUS, 240, 192000, 0, 20, 10, 60),
    Preset(1, "Balanced", CODEC_OPUS, 480, 160000, 1, 40, 20, 120),
    Preset(2, "Robust", CODEC_OPUS, 960, 160000, 2, 80, 40, 200),
    Preset(3, "Uncompressed", CODEC_PCM, 240, 0, 0, 20, 10, 60),
]


def preset(preset_id):
    return PRESETS[preset_id] if 0 <= preset_id < len(PRESETS) else PRESETS[1]


@dataclass
class Packet:
    stream_id: int
    epoch: int
    codec: int
    channels: int
    frame_samples: int
    preset_id: int
    seq: int
    frames: list  # [(seq, payload)], newest first


def parse_packet(data):
    if len(data) < HEADER.size:
        raise ValueError("short packet")
    (magic, version, _flags, stream_id, epoch, codec, channels, frame_samples,
     count, preset_id, seq) = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError("bad magic")
    if version != VERSION:
        raise ValueError(f"unsupported version {version}")
    frames, off = [], HEADER.size
    for i in range(count):
        if off + 2 > len(data):
            raise ValueError("truncated frame length")
        (n,) = struct.unpack_from("<H", data, off)
        off += 2
        if off + n > len(data):
            raise ValueError("truncated frame")
        frames.append((seq - i, bytes(data[off:off + n])))
        off += n
    return Packet(stream_id, epoch, codec, channels, frame_samples, preset_id, seq, frames)


def build_packet(stream_id, epoch, codec, frame_samples, preset_id, seq, payloads):
    """payloads newest first: seq, seq-1, ..."""
    out = bytearray(HEADER.pack(
        MAGIC, VERSION, 0, stream_id, epoch, codec, CHANNELS, frame_samples,
        len(payloads), preset_id, seq,
    ))
    for p in payloads:
        out += struct.pack("<H", len(p)) + p
    return bytes(out)


def percentiles(values):
    if not values:
        return {"count": 0, "p50": 0.0, "p95": 0.0, "p99": 0.0, "max": 0.0}
    s = sorted(values)

    def pct(p):
        return s[min(len(s) - 1, int(p / 100 * len(s)))]

    return {"count": len(s), "p50": pct(50), "p95": pct(95), "p99": pct(99), "max": s[-1]}


def fit_drift_ppm(points):
    """Least-squares slope of media time against arrival time. Positive: the
    sender's clock runs fast relative to ours."""
    n = len(points)
    mx = sum(a for a, _ in points) / n
    my = sum(m for _, m in points) / n
    sxx = sum((a - mx) ** 2 for a, _ in points)
    sxy = sum((a - mx) * (m - my) for a, m in points)
    return (sxy / sxx - 1) * 1e6 if sxx else 0.0


class StreamTracker:
    """Arrival statistics for one stream. feed() every packet in arrival
    order, then close()."""

    def __init__(self):
        self.packets = self.bytes = 0
        self.duplicates = self.reordered = self.epoch_changes = 0
        self.lost = self.recovered = self.frames_expected = 0
        self.jitter_ms = []
        self.gaps_ms = []
        self.drift_ppm = None
        self._drift_span = 0.0
        self._epoch = None
        self._last_arrival = None
        self._first_arrival = None
        self._reset_epoch()

    def _reset_epoch(self):
        self._primary = set()
        self._redundant = set()
        self._first = self._highest = None
        self._min_offset = None
        self._points = []

    def feed(self, p, arrival, size):
        self.packets += 1
        self.bytes += size
        if self._first_arrival is None:
            self._first_arrival = arrival
        if self._last_arrival is not None:
            self.gaps_ms.append((arrival - self._last_arrival) * 1000)
        self._last_arrival = arrival

        if self._epoch is not None and p.epoch != self._epoch:
            self._close_epoch()
            self.epoch_changes += 1
        self._epoch = p.epoch

        if p.seq in self._primary:
            self.duplicates += 1
            return
        if self._highest is not None and p.seq < self._highest:
            self.reordered += 1
        self._primary.add(p.seq)
        for s, _ in p.frames[1:]:
            self._redundant.add(s)
        lowest = p.frames[-1][0]
        self._first = lowest if self._first is None else min(self._first, lowest)
        self._highest = p.seq if self._highest is None else max(self._highest, p.seq)

        media = p.seq * p.frame_samples / SAMPLE_RATE
        offset = arrival - media
        self._min_offset = offset if self._min_offset is None else min(self._min_offset, offset)
        self.jitter_ms.append((offset - self._min_offset) * 1000)
        self._points.append((arrival, media))

    def _close_epoch(self):
        if self._first is None:
            return
        for s in range(self._first, self._highest + 1):
            if s in self._primary:
                continue
            if s in self._redundant:
                self.recovered += 1
            else:
                self.lost += 1
        self.frames_expected += self._highest - self._first + 1
        span = self._points[-1][0] - self._points[0][0]
        if span >= 2.0 and span > self._drift_span:
            self.drift_ppm = fit_drift_ppm(self._points)
            self._drift_span = span
        self._reset_epoch()

    def close(self):
        self._close_epoch()

    def summary(self):
        secs = (self._last_arrival - self._first_arrival) if self.packets > 1 else 0
        return {
            "packets": self.packets,
            "frames_expected": self.frames_expected,
            "lost": self.lost,
            "recovered": self.recovered,
            "duplicates": self.duplicates,
            "reordered": self.reordered,
            "epoch_changes": self.epoch_changes,
            "loss_pct": 100 * self.lost / self.frames_expected if self.frames_expected else 0.0,
            "kbps": self.bytes * 8 / secs / 1000 if secs else 0.0,
            "jitter_ms": percentiles(self.jitter_ms),
            "max_gap_ms": max(self.gaps_ms, default=0.0),
            "drift_ppm": self.drift_ppm,
        }


class JitterSim:
    """Plays a stream out on the receiver's clock after a fixed target delay
    and counts what a jitter buffer would have done. Simpler than RackSim's
    buffer: the target doesn't adapt and there's no drift correction.

    A frame due at play time is fine if it arrived by then (recovered if
    only a redundant copy did), lost if a later frame had arrived (PLC
    covers it), and otherwise an underrun, which rebuffers."""

    REBASE_GAP_SEC = 0.5

    def __init__(self, target_ms):
        self.target = target_ms / 1000
        self.underruns = self.lost = self.recovered = 0
        self._epoch = None
        self._last_arrival = None
        self._restart()

    def _restart(self):
        self._base = None  # play time of seq 0
        self._next = None
        self._seen = {}  # seq -> (first arrival, primary?)
        self._highest = None

    def feed(self, p, arrival):
        gap = self._last_arrival is not None and arrival - self._last_arrival > self.REBASE_GAP_SEC
        if (self._epoch is not None and p.epoch != self._epoch) or gap:
            self._restart()
        self._epoch = p.epoch
        self._last_arrival = arrival
        fs = p.frame_samples / SAMPLE_RATE

        # play out everything due before this packet arrived
        if self._base is not None:
            while self._base + self._next * fs <= arrival:
                due = self._base + self._next * fs
                seen = self._seen.pop(self._next, None)
                if seen is not None and seen[0] <= due:
                    if not seen[1]:
                        self.recovered += 1
                elif self._highest is not None and self._highest > self._next:
                    self.lost += 1
                else:
                    self.underruns += 1
                    self._base = None  # rebuffer from the next arrival
                    break
                self._next += 1

        for i, (s, _) in enumerate(p.frames):
            if self._next is not None and s < self._next:
                continue
            if s not in self._seen:
                self._seen[s] = (arrival, i == 0)
        self._highest = p.seq if self._highest is None else max(self._highest, p.seq)

        if self._base is None:
            self._base = arrival + self.target - p.seq * fs
            if self._next is None:
                self._next = p.seq


def decode_stream(codec, frame_samples, payloads, first, last):
    """Interleaved float samples for seqs first..last; missing frames are
    silence. Opus needs the optional opuslib package."""
    out = []
    silence = [0.0] * (frame_samples * CHANNELS)
    decoder = None
    if codec == CODEC_OPUS:
        import array
        import opuslib  # optional; see requirements.txt

        decoder = opuslib.Decoder(SAMPLE_RATE, CHANNELS)
    for s in range(first, last + 1):
        data = payloads.get(s)
        if data is None:
            out.extend(silence)
        elif codec == CODEC_PCM:
            n = len(data) // 2
            out.extend(v / 32767 for v in struct.unpack(f"<{n}h", data))
        else:
            pcm = decoder.decode_float(data, frame_samples)
            out.extend(array.array("f", pcm))
    return out


def analyze_sine(x, sample_rate, glitch_merge=48, dropout_min=48):
    """Checks a decoded single-tone signal. A glitch is a sample the
    recurrence x[n+1] = 2cos(w)x[n] - x[n-1] predicts badly (more than 10%
    of the amplitude); glitches within glitch_merge samples count once. A
    dropout is a run of at least dropout_min near-silent samples."""
    amp = max((abs(v) for v in x), default=0.0)
    if amp == 0:
        return {"frequency_hz": 0.0, "amplitude": 0.0, "glitches": 0, "dropouts": 0}

    crossings = []
    for i in range(1, len(x)):
        if x[i - 1] < 0 <= x[i]:
            crossings.append(i - 1 + -x[i - 1] / (x[i] - x[i - 1]))
    if len(crossings) < 2:
        return {"frequency_hz": 0.0, "amplitude": amp, "glitches": 0, "dropouts": 0}
    freq = (len(crossings) - 1) * sample_rate / (crossings[-1] - crossings[0])

    k = 2 * math.cos(2 * math.pi * freq / sample_rate)
    glitches, last = 0, -glitch_merge - 1
    for n in range(1, len(x) - 1):
        if abs(x[n + 1] - k * x[n] + x[n - 1]) > 0.1 * amp:
            if n - last > glitch_merge:
                glitches += 1
            last = n

    dropouts, run = 0, 0
    for v in x:
        if abs(v) < 0.01 * amp:
            run += 1
            if run == dropout_min:
                dropouts += 1
        else:
            run = 0
    return {"frequency_hz": freq, "amplitude": amp, "glitches": glitches, "dropouts": dropouts}
```

- [ ] **Step 4: Run the tests and confirm they pass**

Run: `cd tools/bench && .venv/bin/python -m unittest test_audio_analysis -v`
Expected: 15 tests, `OK`.

If `test_steady_stream_no_underrun` or `test_redundant_copy_counts_as_recovered`
fails:
- Trace `JitterSim.feed` by hand for the first 3 packets.
- `_base` must be set from the **first** packet only, then reset only on an
  underrun, an epoch change or a gap.
- Fix the implementation, not the test.

Note: the sine zero-crossing check skips samples exactly at the start, so
`x[0] == 0` doesn't count as a crossing. The dropout test zeroes 480
samples, so it produces exactly one dropout.

- [ ] **Step 5: Stop.** The user reviews and commits.

---

## Task 5: Python test client (`audio_bench.py`) and mock server audio

**Files:**
- Create: `tools/bench/audio_bench.py`
- Modify:
  - `tools/bench/mock_server.py`
  - `tools/bench/overlay_bench.py` (pacing toggle and metrics)
  - `tools/bench/README.md` and `AGENTS.md`
  - `tools/bench/requirements.txt` and `shell.nix`

**Interfaces:**
- Consumes `audio_analysis` (Task 4) and these `overlay_bench` names: `Client`,
  `connect`, `stream`, `client_stats`, `summarize`, `fmt`, `BenchError`,
  `build`, `TOGGLE_ROUTES` and `udp_socket_drops`.
- Produces:
  - **CLI:** `audio_bench.py run [--passive] [--audio-port 7747] [--preset ID]
    [--duration S] [--warmup S] [--drop P] [--overlays N] [--overlay-height PX]
    [--overlay-rate FPS] [--wav PATH] [--analyze] [--no-chunk-pacing]
    [--label L] [--json PATH] [--host H] [--port P] [-v]`
  - **Compare:** `audio_bench.py compare A B`
  - **Mock server:** `mock_server.py --audio-to HOST:PORT` and
    `mock_server.py --audio`

- [ ] **Step 1: Add the pacing toggle to `overlay_bench.py`**

`TOGGLE_ROUTES` gains `"/bench/chunk_pacing"`:

```python
TOGGLE_ROUTES = (
    "/bench/overlay_cache", "/bench/prep_worker", "/bench/adaptive_retry", "/bench/back_pressure",
    "/bench/supersede", "/bench/chunk_pacing",
)
```

`TRANSPORT_HEADLINE` gains `"transport.pacing_rate_mbps"` and
`"transport.pacing_wait_ms"`.

In `transport_stats` (around line 684), add to the returned dict. Its keys
carry no prefix; `flatten` adds `transport.`:

```python
        "pacing_rate_mbps": c.get("tx.pacing.rate_mbps", 0),
        "pacing_wait_ms": stat_p50("tx.pacing.wait"),
```

In `cmd_run`'s toggle tuple add
`("/bench/chunk_pacing", args.chunk_pacing, "chunk pacing"),`.

In `main()` add:

```python
    run.add_argument(
        "--no-chunk-pacing", dest="chunk_pacing", action="store_false",
        help="don't pace texture chunks for a managed bCast audio stream",
    )
```

- [ ] **Step 2: Add audio to `mock_server.py`**

Add `"/bench/chunk_pacing"` to `TOGGLES`. Add these imports and helpers
after the existing ones:

```python
import math
import os
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import audio_analysis as aa  # noqa: E402

AUDIO_STREAM_ID = 4242
AUDIO_MODULE_ID = 4242


class AudioSender:
    """Fake bCast: a 1 kHz PCM sine (Uncompressed preset, 5 ms frames)."""

    def __init__(self, dest):
        self.dest = dest
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.epoch = 0
        self.running = False
        self.thread = None

    def start(self):
        if self.running:
            return
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def stop(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=1)
        self.epoch = (self.epoch + 1) & 0xFFFF

    def _loop(self):
        fs = 240
        seq = 0
        start = time.perf_counter()
        while self.running:
            due = start + seq * fs / aa.SAMPLE_RATE
            now = time.perf_counter()
            if now < due:
                time.sleep(due - now)
            samples = []
            for i in range(fs):
                v = int(16383 * math.sin(2 * math.pi * 1000 * (seq * fs + i) / aa.SAMPLE_RATE))
                samples += [v, v]
            payload = aa.struct.pack(f"<{len(samples)}h", *samples)
            self.sock.sendto(
                aa.build_packet(AUDIO_STREAM_ID, self.epoch, aa.CODEC_PCM, fs, 3, seq, [payload]),
                self.dest,
            )
            seq += 1
```

In `main()`, add the arguments:

```python
    ap.add_argument("--audio-to", metavar="HOST:PORT",
                    help="also send a fake standalone bCast stream here")
    ap.add_argument("--audio", action="store_true",
                    help="act as OSCctrl with a managed bCast next to it")
```

After `print(f"mock OSCctrl listening ...")`:

```python
    if args.audio_to:
        host, port = args.audio_to.rsplit(":", 1)
        AudioSender((host, int(port))).start()
        print(f"sending fake bCast audio to {host}:{port}", flush=True)
    managed = None  # AudioSender while a client has enabled the managed stream

    def audio_stream_msg(enabled):
        epoch = managed.epoch if managed else 0
        return msg("/set/audio_stream", ("h", AUDIO_MODULE_ID), ("i", AUDIO_STREAM_ID),
                   ("i", 3), ("i", int(enabled)), ("i", epoch))
```

In the `/register` branch, after the heartbeat `send`:

```python
                if args.audio:
                    if managed:
                        managed.stop()
                        managed = None
                    send(audio_stream_msg(False))
```

Add new `elif` branches before `elif a == "/get/module_stubs":`:

```python
            elif a == "/set/audio/enabled" and args.audio:
                ok = p[0] == AUDIO_MODULE_ID
                if ok and p[1] and not managed:
                    managed = AudioSender((client[0], p[2]))
                    managed.start()
                elif ok and not p[1] and managed:
                    managed.stop()
                    managed = None
                send(msg("/set/audio/enabled/ack", ("h", p[0]), ("i", p[1]), ("i", int(ok))))
                if ok:
                    send(audio_stream_msg(bool(managed)))
            elif a == "/set/audio/preset" and args.audio:
                # always PCM; ack the request and keep sending preset 3
                ok = p[0] == AUDIO_MODULE_ID and 0 <= p[1] <= 3
                send(msg("/set/audio/preset/ack", ("h", p[0]), ("i", p[1]), ("i", int(ok))))
            elif a == "/audio/stats":
                print("audio stats", *p[1:], flush=True)
```

Also add `msg("/bench/counter", ("s", "tx.pacing.rate_mbps"), ("h", 0)),` to
the canned report list.

- [ ] **Step 3: Write `tools/bench/audio_bench.py`**

```python
#!/usr/bin/env python3
"""bCast audio stream test client.

Managed (default): registers with OSCctrl like RackSim does, enables the
bCast next to it, and reports /audio/stats from a simulated jitter buffer.
--passive: only listens, for a standalone bCast pointed at this machine.

See README.md ("audio bench").
"""

import argparse
import json
import random
import socket
import sys
import threading
import time
import wave

import audio_analysis as aa
from overlay_bench import (
    BenchError, Client, build, client_stats, connect, fmt, stream, udp_socket_drops,
)

DEFAULT_AUDIO_PORT = 7747
STATS_INTERVAL = 0.5


class AudioReceiver:
    """Receives audio packets on their own socket and thread, like RackSim."""

    def __init__(self, port, drop_rate=0.0, verbose=False):
        self.port = port
        self.drop_rate = drop_rate
        self.verbose = verbose
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
        try:
            self.sock.bind(("0.0.0.0", port))
        except OSError as e:
            raise BenchError(f"can't bind UDP {port} for audio ({e})")
        self.sock.settimeout(0.2)
        self.lock = threading.Lock()
        self.stream_id = None  # accept only this stream once set
        self.recording = False
        self.records = []  # (packet, arrival, size)
        self.invalid = self.foreign = self.sim_dropped = 0
        self.sim = None  # JitterSim fed live, for /audio/stats
        self.sim_tracker = aa.StreamTracker()  # live jitter for stats
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()

    def close(self):
        self.running = False
        self.thread.join(timeout=1)
        self.sock.close()

    def _loop(self):
        while self.running:
            try:
                data, _ = self.sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                return
            arrival = time.perf_counter()
            try:
                p = aa.parse_packet(data)
            except ValueError as e:
                self.invalid += 1
                if self.verbose:
                    print(f"  ! bad audio packet: {e}", file=sys.stderr)
                continue
            with self.lock:
                if self.stream_id is None:
                    self.stream_id = p.stream_id
                if p.stream_id != self.stream_id:
                    self.foreign += 1
                    continue
                if self.drop_rate and random.random() < self.drop_rate:
                    self.sim_dropped += 1
                    continue
                if self.sim is None:
                    self.sim = aa.JitterSim(aa.preset(p.preset_id).target_ms)
                self.sim.feed(p, arrival)
                self.sim_tracker.feed(p, arrival, len(data))
                if self.recording:
                    self.records.append((p, arrival, len(data)))

    def start_recording(self):
        with self.lock:
            self.records = []
            self.recording = True

    def stop_recording(self):
        with self.lock:
            self.recording = False
            return list(self.records)

    def stats_args(self):
        """/audio/stats arguments after the module id: cumulative counts from
        the live simulated buffer, recent jitter p95 and the target"""
        with self.lock:
            sim = self.sim
            recent = self.sim_tracker.jitter_ms[-100:]
            if sim is None:
                return 0, 0, 0, 0.0, 0.0
            return (sim.underruns, sim.lost, sim.recovered,
                    float(aa.percentiles(recent)["p95"]), float(sim.target * 1000))


class AudioClient(Client):
    """overlay_bench's Client plus the managed audio routes."""

    def __init__(self, verbose=False):
        super().__init__(verbose=verbose)
        self.audio_streams = {}  # module id -> latest /set/audio_stream args
        self.audio_acks = {}  # route -> (module id, value, success)

    def _dispatch(self, msg, addr, received_at):
        a, p = msg.address, msg.params
        if a == "/set/audio_stream":
            self.audio_streams[p[0]] = {
                "module_id": p[0], "stream_id": p[1], "preset": p[2],
                "enabled": bool(p[3]), "epoch": p[4],
            }
        elif a in ("/set/audio/enabled/ack", "/set/audio/preset/ack"):
            self.audio_acks[a] = (p[0], p[1], bool(p[2]))
        else:
            super()._dispatch(msg, addr, received_at)

    def wait_stream(self, timeout):
        if not self.wait_for(lambda: self.audio_streams, timeout):
            raise BenchError(
                "no /set/audio_stream. is a bCast placed directly next to OSCctrl?"
            )
        return next(iter(self.audio_streams.values()))

    def request(self, route, module_id, *args):
        ack = route + "/ack"
        self.audio_acks.pop(ack, None)
        self.send(route, ("h", module_id), *args)
        if not self.wait_for(lambda: ack in self.audio_acks, 3):
            raise BenchError(f"no {ack}")
        if not self.audio_acks[ack][2]:
            raise BenchError(f"{route} failed")


def stats_loop(client, receiver, module_id, stop):
    while not stop.wait(STATS_INTERVAL):
        args = receiver.stats_args()
        client.send(
            "/audio/stats", ("h", module_id), ("i", args[0]), ("i", args[1]),
            ("i", args[2]), ("f", args[3]), ("f", args[4]),
        )


def analyze(records, wav_path, do_analyze):
    """Arrival stats, a fresh simulated buffer and optional decode."""
    tracker = aa.StreamTracker()
    sim = None
    payloads = {}  # (epoch, seq) -> payload
    codec = frame_samples = None
    for p, arrival, size in records:
        tracker.feed(p, arrival, size)
        if sim is None:
            sim = aa.JitterSim(aa.preset(p.preset_id).target_ms)
        sim.feed(p, arrival)
        codec, frame_samples = p.codec, p.frame_samples
        for s, data in p.frames:
            payloads.setdefault((p.epoch, s), data)
    tracker.close()
    result = tracker.summary()
    result["sim_underruns"] = sim.underruns if sim else 0
    result["sim_lost"] = sim.lost if sim else 0
    result["sim_recovered"] = sim.recovered if sim else 0

    if (wav_path or do_analyze) and records:
        last_epoch = records[-1][0].epoch
        seqs = [s for (e, s) in payloads if e == last_epoch]
        by_seq = {s: d for (e, s), d in payloads.items() if e == last_epoch}
        try:
            samples = aa.decode_stream(codec, frame_samples, by_seq, min(seqs), max(seqs))
        except ImportError:
            raise BenchError("decoding Opus needs opuslib: pip install opuslib")
        if wav_path:
            with wave.open(wav_path, "wb") as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(aa.SAMPLE_RATE)
                w.writeframes(aa.struct.pack(
                    f"<{len(samples)}h",
                    *(int(max(-1.0, min(1.0, v)) * 32767) for v in samples),
                ))
            print(f"wrote {wav_path}")
        if do_analyze:
            # skip the codec's first frames and the last (possibly partial) one
            left = samples[0::2][frame_samples * 4:-frame_samples]
            result["sine"] = aa.analyze_sine(left, aa.SAMPLE_RATE)
    return result


def cmd_run(args):
    if not 0 <= args.drop < 1:
        raise BenchError("--drop must be in [0, 1)")
    if args.passive and (args.preset is not None or args.overlays):
        raise BenchError("--preset and --overlays need managed mode")

    receiver = AudioReceiver(args.audio_port, args.drop, args.verbose)
    client = None
    stop_stats = threading.Event()
    stats_thread = overlay_thread = None
    overlay_frames = {}
    server = {}
    restore_pacing = False
    stream_info = None
    try:
        if not args.passive:
            client = AudioClient(verbose=args.verbose)
            connect(client, args)
            stream_info = client.wait_stream(5)
            module_id = stream_info["module_id"]
            print(f"bCast module {module_id}, stream {stream_info['stream_id']}")
            if args.preset is not None:
                client.request("/set/audio/preset", module_id, ("i", args.preset))
            client.request("/set/audio/enabled", module_id, ("i", 1), ("i", args.audio_port))
            try:
                client.bench_reset()
                server["bench"] = True
            except BenchError:
                server["bench"] = False
                if args.overlays or not args.chunk_pacing:
                    raise
            if server["bench"]:
                client.set_toggle("/bench/chunk_pacing", args.chunk_pacing)
                restore_pacing = not args.chunk_pacing
                print(f"chunk pacing: {'on' if args.chunk_pacing else 'OFF'}")
            stats_thread = threading.Thread(
                target=stats_loop, args=(client, receiver, module_id, stop_stats), daemon=True
            )
            stats_thread.start()

        if not receiver.thread.is_alive():
            raise BenchError("audio receiver stopped")
        deadline = time.monotonic() + 5
        while receiver.stream_id is None:
            if time.monotonic() > deadline:
                raise BenchError(f"no audio packets on UDP {args.audio_port}")
            time.sleep(0.05)

        if args.warmup > 0:
            print(f"warmup: {args.warmup:.1f}s...")
            time.sleep(args.warmup)

        texture_ids = []
        if args.overlays:
            stubs = []
            client.module_stubs, client.module_count = [], None
            client.send("/get/module_stubs")
            if not client.wait_for(lambda: client.module_count is not None, 5):
                raise BenchError("no module stubs received")
            stubs = [m for m in client.module_stubs if m["plugin"] != "gotnosoft"]
            if len(stubs) < args.overlays:
                raise BenchError(f"--overlays {args.overlays} but the patch has {len(stubs)} other modules")
            targets = client.find_targets(None, None, 0, [m["id"] for m in stubs[: args.overlays]])
            texture_ids = [t["texture_id"] for t in targets]

        print(f"measure: {args.duration:.1f}s"
              + (f" with {args.overlays} overlays" if args.overlays else "") + "...")
        if client and server.get("bench"):
            client.bench_reset()
        drops_before = udp_socket_drops(args.audio_port)
        receiver.start_recording()
        start = time.perf_counter()
        if texture_ids:
            def run_overlays():
                overlay_frames.update(stream(
                    client, texture_ids, [("i", args.overlay_height)], args.overlay_rate,
                    args.duration, 2.0,
                ))
            overlay_thread = threading.Thread(target=run_overlays, daemon=True)
            overlay_thread.start()
            overlay_thread.join()
        else:
            time.sleep(args.duration)
        records = receiver.stop_recording()
        elapsed = time.perf_counter() - start
        drops_after = udp_socket_drops(args.audio_port)
        if client and server.get("bench"):
            server["report"] = client.bench_report()
    finally:
        stop_stats.set()
        if stats_thread:
            stats_thread.join(timeout=1)
        if client:
            try:
                if restore_pacing:
                    client.set_toggle("/bench/chunk_pacing", True)
                if stream_info:
                    client.request("/set/audio/enabled", stream_info["module_id"], ("i", 0), ("i", 0))
            except BenchError as e:
                print(f"  ! cleanup: {e}", file=sys.stderr)
            client.close()
        receiver.close()

    audio = analyze(records, args.wav, args.analyze)
    audio["invalid_packets"] = receiver.invalid
    audio["foreign_packets"] = receiver.foreign
    audio["sim_dropped"] = receiver.sim_dropped
    if drops_before is not None and drops_after is not None:
        audio["socket_drops"] = drops_after - drops_before
    if records:
        audio["preset"] = records[-1][0].preset_id

    result = {
        "label": args.label,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "args": {k: v for k, v in vars(args).items() if k != "func"},
        "audio": audio,
    }
    if overlay_frames:
        result["overlays"] = client_stats(overlay_frames, elapsed)
    if "report" in server:
        counters = server["report"]["counters"]
        stats = server["report"]["stats"]
        result["server"] = {
            "pacing_rate_mbps": counters.get("tx.pacing.rate_mbps", 0),
            "pacing_wait_ms": stats.get("tx.pacing.wait", {}).get("p50", 0),
            "client_underruns": counters.get("audio.client.underruns", 0),
            "client_lost": counters.get("audio.client.lost", 0),
        }
    print_result(result)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(result, f, indent=2)
        print(f"\nwrote {args.json}")
    return 0


def print_result(r):
    a = r["audio"]
    j = a["jitter_ms"]
    preset = aa.preset(a.get("preset", 1))
    print(f"\naudio ({preset.name}): {a['packets']} packets, {fmt(a['kbps'])} kbit/s")
    print(f"  jitter ms     p50 {fmt(j['p50'])}  p95 {fmt(j['p95'])}  "
          f"p99 {fmt(j['p99'])}  max {fmt(j['max'])}")
    print(f"  max gap ms    {fmt(a['max_gap_ms'])}")
    print(f"  frames        {a['frames_expected']} expected, {a['lost']} lost "
          f"({fmt(a['loss_pct'])}%), {a['recovered']} recovered by redundancy")
    print(f"  order         {a['reordered']} reordered, {a['duplicates']} duplicates, "
          f"{a['epoch_changes']} epoch changes")
    print(f"  drift         {fmt(a['drift_ppm']) if a['drift_ppm'] is not None else 'n/a'} ppm")
    print(f"  sim buffer    {a['sim_underruns']} underruns, {a['sim_lost']} PLC, "
          f"{a['sim_recovered']} recovered (target {preset.target_ms} ms)")
    if "socket_drops" in a:
        print(f"  socket drops  {a['socket_drops']}")
    if "sine" in a:
        s = a["sine"]
        print(f"  sine          {fmt(s['frequency_hz'])} Hz, {s['glitches']} glitches, "
              f"{s['dropouts']} dropouts")
    if "overlays" in r:
        o = r["overlays"]
        print(f"\noverlays: {fmt(o['total_fps'])} fps total, {fmt(o['fps'])} per overlay")
        for name, s in sorted(o["stats"].items()):
            print(f"  {name:<34} p50 {fmt(s['p50'])}  p95 {fmt(s['p95'])}")
    if "server" in r:
        s = r["server"]
        print(f"\nserver: pacing {fmt(s['pacing_rate_mbps'])} Mbit/s, "
              f"wait p50 {fmt(s['pacing_wait_ms'])} ms, client underruns "
              f"{s['client_underruns']}, lost {s['client_lost']}")


def flatten(d, prefix=""):
    out = {}
    for k, v in d.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            out.update(flatten(v, key + "."))
        elif isinstance(v, (int, float)) and not isinstance(v, bool):
            out[key] = v
    return out


def cmd_compare(args):
    with open(args.before) as f:
        before = json.load(f)
    with open(args.after) as f:
        after = json.load(f)
    print(f"before: {before.get('label') or args.before}")
    print(f"after:  {after.get('label') or args.after}\n")
    a = flatten({k: before[k] for k in ("audio", "overlays", "server") if k in before})
    b = flatten({k: after[k] for k in ("audio", "overlays", "server") if k in after})
    for key in sorted(set(a) | set(b)):
        x, y = a.get(key), b.get(key)
        if x is None or y is None:
            continue
        delta = y - x
        print(f"  {key:<44} {fmt(x):>10} {fmt(y):>10} {'+' if delta >= 0 else ''}{fmt(delta):>9}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)

    run = sub.add_parser("run", help="measure a bCast stream")
    run.add_argument("--host", help="OSCctrl host (default: discover)")
    run.add_argument("--port", type=int, default=7225, help="OSCctrl listen port")
    run.add_argument("--discover-timeout", type=float, default=3.0)
    run.add_argument("-v", "--verbose", action="store_true")
    run.add_argument("--passive", action="store_true", help="only listen (standalone bCast)")
    run.add_argument("--audio-port", type=int, default=DEFAULT_AUDIO_PORT)
    run.add_argument("--preset", type=int, choices=range(len(aa.PRESETS)),
                     help="request this preset (managed)")
    run.add_argument("--duration", type=float, default=10)
    run.add_argument("--warmup", type=float, default=1)
    run.add_argument("--drop", type=float, default=0.0, metavar="P",
                     help="discard this fraction of audio packets")
    run.add_argument("--overlays", type=int, default=0, metavar="N",
                     help="stream N overlays at the same time (managed, BENCH=1 build)")
    run.add_argument("--overlay-height", type=int, default=512)
    run.add_argument("--overlay-rate", type=float, default=60)
    run.add_argument("--no-chunk-pacing", dest="chunk_pacing", action="store_false",
                     help="turn texture chunk pacing off for this run")
    run.add_argument("--wav", help="decode the last epoch and write it here")
    run.add_argument("--analyze", action="store_true",
                     help="with a sine patched into bCast, count glitches and dropouts")
    run.add_argument("--label", default="")
    run.add_argument("--json")
    run.set_defaults(func=cmd_run)

    compare = sub.add_parser("compare", help="compare two JSON results")
    compare.add_argument("before")
    compare.add_argument("after")
    compare.set_defaults(func=cmd_compare)

    args = parser.parse_args()
    try:
        return args.func(args)
    except BenchError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Test passive mode against the mock sender**

In an async shell:

```bash
cd tools/bench && .venv/bin/python mock_server.py --port 7299 --audio-to 127.0.0.1:7747
```

In another shell:

```bash
cd tools/bench && .venv/bin/python audio_bench.py run --passive --duration 3 --warmup 0.5 --analyze --json /tmp/passive.json
```

Expected:
- about 600 packets, about 1540 kbit/s and 0 lost;
- sine about 1000 Hz with 0 glitches and 0 dropouts. The mock's sine is
  continuous by seq, so a glitch means the client lost or misordered frames;
- `/tmp/passive.json` exists.

Stop the mock server with the shell tool.

- [ ] **Step 5: Test managed mode against `mock_server --audio`**

In an async shell:

```bash
cd tools/bench && .venv/bin/python mock_server.py --audio
```

In another shell:

```bash
cd tools/bench && .venv/bin/python audio_bench.py run --host 127.0.0.1 --duration 3 --warmup 0.5 --preset 3 --json /tmp/managed.json
```

Expected:
- the output shows `bCast module 4242, stream 4242` and `chunk pacing: on`;
- about 600 packets and 0 lost;
- the mock prints `audio stats ...` lines about every 500 ms.

Then:

```bash
.venv/bin/python audio_bench.py run --host 127.0.0.1 --duration 3 --warmup 0.5 --overlays 2 --overlay-height 64 --no-chunk-pacing --json /tmp/managed2.json
```

Expected:
- the mock prints `/bench/chunk_pacing 0`, then `/bench/chunk_pacing 1`;
- an `overlays:` section prints.

Then compare the two runs:

```bash
.venv/bin/python audio_bench.py compare /tmp/managed.json /tmp/managed2.json
```

It prints rows. Stop the mock.

- [ ] **Step 6: Re-run the overlay bench against the mock**

This is a regression check on the toggle change. In an async shell:

```bash
cd tools/bench && .venv/bin/python mock_server.py
```

In another shell:

```bash
cd tools/bench && .venv/bin/python overlay_bench.py run --host 127.0.0.1 --duration 1 --idle 0.3 --warmup 0 --no-chunk-pacing --json /tmp/ov.json
```

Expected: the run completes and prints `chunk pacing: OFF`. Stop the mock.

- [ ] **Step 7: Dependencies and docs**

`requirements.txt`:

```
python-osc>=1.8
# optional: decode Opus for audio_bench --wav/--analyze (needs the libopus shared library)
opuslib>=3.0
```

`shell.nix`: the python package list becomes
`[ python-osc opuslib ]`, and add `pkgs.libopus` to `packages`.

`README.md`:
- Add a top-level section `## Audio bench` after the overlay `## Metrics`
  section, covering:
  - what it measures;
  - passive mode (point a standalone bCast at this machine's IP, port 7747);
  - managed mode (bCast next to OSCctrl);
  - `--overlays N` for A/B with `--no-chunk-pacing`;
  - `--analyze` (patch a VCO sine into bCast);
  - `--wav`.
- Add a metrics table:

  | Metric | Meaning |
  |---|---|
  | `jitter_ms` | arrival time minus the frame's media time, relative to the minimum |
  | `max_gap_ms` | largest time between packets |
  | `lost` / `recovered` | frames never received / received only as a redundant copy |
  | `reordered` / `duplicates` | packets older than the newest seen / repeated seq |
  | `epoch_changes` | stream resets |
  | `drift_ppm` | sender clock rate relative to ours |
  | `sim_underruns` / `sim_lost` | what a fixed-target buffer at the preset target would have done |
  | `server.pacing_rate_mbps` | chunk pacing rate at the end (0 = unlimited) |
  | `server.client_underruns` | last reported `/audio/stats` underruns |

- Add `--no-chunk-pacing`, `transport.pacing_rate_mbps` and
  `transport.pacing_wait_ms` to the overlay bench's toggles and transport
  tables.

`AGENTS.md`:
- Add `/bench/chunk_pacing` to the toggle list.
- Add a `## Audio bench` section:
  - test with `mock_server.py --audio` (managed) or `--audio-to
    127.0.0.1:7747` (passive), always with `--host 127.0.0.1` in managed
    mode;
  - run `test_audio_analysis.py` after changing `audio_analysis.py`;
  - `tests/run.sh` covers the C++ packet, encoder and pacer.

- [ ] **Step 8: Stop.** The user reviews and commits.

---

## Task 6: bCast module, panel and registration

**Files:**
- Create: `src/audio/bCast.hpp`, `src/audio/bCast.cpp`
- Create: `tools/panel/bcast_panel.py`, `res/bCast.svg` (generated)
- Create (stub, filled in by Task 7): `src/audio/AudioStreamer.hpp`, `src/audio/AudioStreamer.cpp`
- Modify: `src/plugin.hpp`, `src/plugin.cpp`, `plugin.json`

**Interfaces:**
- Consumes: `bcast::PRESETS`, `presetById`, `isValidPreset`, `DEFAULT_PRESET`
  (Task 2, `Presets.hpp`) and `DEFAULT_PORT` (Task 2, `Packet.hpp`).
- Produces (global namespace):
  - `struct bCastConfig { bool enabled; std::string host; int port; int preset; }`
    with a defaulted `operator==`.
  - `struct bCastClientStats { int underruns, lost, recovered; float jitterMs, bufferMs; bool valid; }`
  - `struct bCastSnapshot { bCastConfig config; bool managed; uint64_t version; }`
  - `struct bCast : Module` with:
    - `LEVEL_PARAM`, `LEFT_INPUT`, `RIGHT_INPUT`, `STATUS_LIGHT` (RGB, 3),
      `VU_LIGHTS` (12);
    - streamer-facing state: `ring`, `streaming`, `engineSampleRate`,
      `ringOverflows`, `epoch`, `sendFailing`;
    - `uint32_t streamId() const`;
    - config API, safe from any thread: `snapshot()`, `isManaged()`,
      `standaloneConfig()`, `setStandalone(fn)`, `setManaged(bool)`,
      `setManagedEnabled(bool, host, port)`, `setManagedPreset(int)`,
      `setClientStats(stats)`, `clientStats()`.
  - `extern Model* modelbCast` (in `plugin.hpp`).

Notes (additions to the deviations list):
- **Panel labels are glyph paths.** Rack's SVG renderer can't draw text, and
  `Renderer::renderPanel` hides every widget child except the panel. A
  label widget would never reach RackSim's panel texture, so
  `tools/panel/bcast_panel.py` turns the labels into paths with fontTools.
- **OSCctrl surrogates.** `util::makeConnectedModuleWidget` adds a fresh
  `bCast` to the engine for some texture renders. Its default config is
  disabled, so its streamer thread idles (one 20 ms wakeup) and sends
  nothing.

- [ ] **Step 1: Generate the panel**

`tools/panel/bcast_panel.py`:

```python
#!/usr/bin/env python3
"""Writes res/bCast.svg. Rack's SVG renderer has no text support, and
OSCctrl's panel renders show only the SVG, so the labels are glyph paths.

  python tools/panel/bcast_panel.py [--font PATH] [--out PATH]
"""
import argparse
import pathlib

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_FONT = ROOT.parent / "rack-git" / "res" / "fonts" / "Nunito-Bold.ttf"
WIDTH, HEIGHT = 25.4, 128.5  # 5HP, mm
INK = "#1e1e1e"

# (text, centre x, centre y, cap height in mm)
LABELS = [
    ("bCast", 12.7, 7.5, 3.6),
    ("LEVEL", 12.7, 57.5, 2.0),
    ("L", 12.7, 82.5, 2.0),
    ("R", 12.7, 98.5, 2.0),
    ("gotnosoft", 12.7, 118.0, 1.6),
]


def text_path(font, text, cx, cy, cap_mm):
    glyphs = font.getGlyphSet()
    cmap = font.getBestCmap()
    cap_units = font["OS/2"].sCapHeight or 0.7 * font["head"].unitsPerEm
    scale = cap_mm / cap_units
    names = [cmap[ord(ch)] for ch in text]
    x = cx - sum(glyphs[n].width for n in names) * scale / 2
    baseline = cy + cap_mm / 2
    pen = SVGPathPen(glyphs, ntos=lambda v: f"{v:.3f}".rstrip("0").rstrip("."))
    for name in names:
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x, baseline)))
        x += glyphs[name].width * scale
    return pen.getCommands()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", type=pathlib.Path, default=DEFAULT_FONT)
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "res" / "bCast.svg")
    args = ap.parse_args()
    font = TTFont(args.font)

    paths = "\n".join(
        f'  <path fill="{INK}" d="{text_path(font, *label)}"/>' for label in LABELS
    )
    svg = f"""<?xml version="1.0" encoding="UTF-8"?>
<!-- generated by tools/panel/bcast_panel.py -->
<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}mm" height="{HEIGHT}mm" viewBox="0 0 {WIDTH} {HEIGHT}">
  <rect width="{WIDTH}" height="{HEIGHT}" fill="#e6e6e6"/>
  <rect x="1.5" y="21" width="22.4" height="35" rx="1" fill="#d2d2d2"/>
  <rect x="5.2" y="78.5" width="15" height="34" rx="1" fill="#d2d2d2"/>
{paths}
</svg>
"""
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(svg)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
```

Run (fontTools isn't in the bench venv; this uses a throwaway one):

```bash
python3 -m venv /tmp/panel-venv && /tmp/panel-venv/bin/pip -q install fonttools
/tmp/panel-venv/bin/python tools/panel/bcast_panel.py
rm -rf /tmp/panel-venv
```

Expected: `wrote .../res/bCast.svg`, about 16 KB, starting with
`<svg ... width="25.4mm" height="128.5mm" viewBox="0 0 25.4 128.5">`.

- [ ] **Step 2: Stub the streamer so the module compiles**

Task 7 replaces both files. `src/audio/AudioStreamer.hpp`:

```cpp
#pragma once

struct bCast;

namespace bcast {

class AudioStreamer {
public:
  explicit AudioStreamer(bCast* module) : module(module) {}
  void start() {}
  void stop() {}

private:
  bCast* module;
};

}
```

`src/audio/AudioStreamer.cpp`:

```cpp
#include "audio/AudioStreamer.hpp"
```

- [ ] **Step 3: Write `src/audio/bCast.hpp`**

```cpp
#pragma once
#include "plugin.hpp"
#include "audio/Packet.hpp"
#include "audio/Presets.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace bcast { class AudioStreamer; }

struct bCastConfig {
  bool enabled = false;
  std::string host = "127.0.0.1";
  int port = bcast::DEFAULT_PORT;
  int preset = bcast::DEFAULT_PRESET;

  bool operator==(const bCastConfig&) const = default;
};

// the client's latest /audio/stats, cumulative counts
struct bCastClientStats {
  int underruns = 0;
  int lost = 0;
  int recovered = 0;
  float jitterMs = 0.f;
  float bufferMs = 0.f;
  bool valid = false;
};

struct bCastSnapshot {
  bCastConfig config;
  bool managed = false;
  uint64_t version = 0;
};

struct bCast : Module {
  enum ParamId { LEVEL_PARAM, PARAMS_LEN };
  enum InputId { LEFT_INPUT, RIGHT_INPUT, INPUTS_LEN };
  enum OutputId { OUTPUTS_LEN };
  enum LightId { ENUMS(STATUS_LIGHT, 3), ENUMS(VU_LIGHTS, 12), LIGHTS_LEN };

  // engine thread -> streamer thread (single producer, single consumer).
  // 16384 frames is 340 ms at 48 kHz, 85 ms at 192 kHz.
  dsp::RingBuffer<dsp::Frame<2>, 16384> ring;
  // set by the streamer; process() only pushes while it's true
  std::atomic<bool> streaming{false};
  std::atomic<float> engineSampleRate{0.f};
  std::atomic<uint32_t> ringOverflows{0};
  std::atomic<uint16_t> epoch{0};
  std::atomic<bool> sendFailing{false};
  bool dcFilterEnabled = true;

  bCast();
  ~bCast() override;

  void process(const ProcessArgs& args) override;
  void onAdd(const AddEvent& e) override;
  void onRemove(const RemoveEvent& e) override;
  void onSampleRateChange(const SampleRateChangeEvent& e) override;
  json_t* dataToJson() override;
  void dataFromJson(json_t* rootJ) override;

  uint32_t streamId() const { return (uint32_t)id; }

  // config API, any thread. version bumps whenever the effective config
  // (standalone, or managed while attached to OSCctrl) changes
  bCastSnapshot snapshot();
  bool isManaged() const { return managed.load(); }
  bCastConfig standaloneConfig();
  void setStandalone(const std::function<void(bCastConfig&)>& edit);
  // attaching starts managed mode disabled; the client enables it
  void setManaged(bool on);
  void setManagedEnabled(bool enabled, const std::string& host, int port);
  void setManagedPreset(int preset);
  void setClientStats(const bCastClientStats& stats);
  bCastClientStats clientStats();

private:
  dsp::RCFilter dcFilters[2];
  dsp::VuMeter2 vuMeters[2];
  dsp::ClockDivider lightDivider;

  std::mutex configMutex;
  bCastConfig standalone;
  bCastConfig managedConfig;
  std::atomic<bool> managed{false};  // written under configMutex
  uint64_t version = 0;
  bCastClientStats stats;

  std::unique_ptr<bcast::AudioStreamer> streamer;

  const bCastConfig& effective() const { return managed ? managedConfig : standalone; }
  // runs change under the lock and bumps version if the effective config moved
  void update(const std::function<void()>& change);
  void stopStreamer();
};

struct bCastWidget : ModuleWidget {
  bCastWidget(bCast* module);
  void appendContextMenu(Menu* menu) override;
};
```

- [ ] **Step 4: Write `src/audio/bCast.cpp`**

```cpp
#include "audio/bCast.hpp"
#include "audio/AudioStreamer.hpp"

#include <cmath>
#include <cstdlib>

bCast::bCast() {
  config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
  configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "Level", " dB", -10, 40);
  configInput(LEFT_INPUT, "Left");
  configInput(RIGHT_INPUT, "Right");
  configLight(STATUS_LIGHT, "Status");
  lightDivider.setDivision(512);
  for (auto& f : dcFilters) f.setCutoffFreq(10.f / 48000.f);
}

bCast::~bCast() {
  stopStreamer();
}

void bCast::stopStreamer() {
  if (!streamer) return;
  streamer->stop();
  streamer.reset();
}

void bCast::onAdd(const AddEvent& e) {
  if (!streamer) streamer = std::make_unique<bcast::AudioStreamer>(this);
  streamer->start();
}

void bCast::onRemove(const RemoveEvent& e) {
  stopStreamer();
}

void bCast::onSampleRateChange(const SampleRateChangeEvent& e) {
  for (auto& f : dcFilters) f.setCutoffFreq(10.f * e.sampleTime);
  engineSampleRate = e.sampleRate;
}

void bCast::process(const ProcessArgs& args) {
  const float gain = std::pow(params[LEVEL_PARAM].getValue(), 2.f);
  const bool rightConnected = inputs[RIGHT_INPUT].isConnected();

  dsp::Frame<2> frame;
  for (int c = 0; c < 2; ++c) {
    int input = (c == 1 && rightConnected) ? RIGHT_INPUT : LEFT_INPUT;
    float v = inputs[input].getVoltageSum() / 10.f;
    if (dcFilterEnabled) {
      dcFilters[c].process(v);
      v = dcFilters[c].highpass();
    }
    v *= gain;
    frame.samples[c] = v;
    vuMeters[c].process(args.sampleTime, v);
  }

  if (streaming.load(std::memory_order_relaxed)) {
    if (ring.full()) ringOverflows.fetch_add(1, std::memory_order_relaxed);
    else ring.push(frame);
  }

  if (lightDivider.process()) {
    float r = 0.f, g = 0.f, b = 0.f;
    bool on = streaming.load(std::memory_order_relaxed);
    if (sendFailing.load(std::memory_order_relaxed)) r = 1.f;
    else if (managed.load(std::memory_order_relaxed)) b = on ? 1.f : 0.3f;
    else if (on) g = 1.f;
    lights[STATUS_LIGHT + 0].setBrightness(r);
    lights[STATUS_LIGHT + 1].setBrightness(g);
    lights[STATUS_LIGHT + 2].setBrightness(b);

    for (int c = 0; c < 2; ++c) {
      int base = VU_LIGHTS + c * 6;
      lights[base + 0].setBrightness(vuMeters[c].getBrightness(0, 0));
      lights[base + 1].setBrightness(vuMeters[c].getBrightness(-6, -3));
      lights[base + 2].setBrightness(vuMeters[c].getBrightness(-12, -6));
      lights[base + 3].setBrightness(vuMeters[c].getBrightness(-24, -12));
      lights[base + 4].setBrightness(vuMeters[c].getBrightness(-36, -24));
      lights[base + 5].setBrightness(vuMeters[c].getBrightness(-48, -36));
    }
  }
}

void bCast::update(const std::function<void()>& change) {
  std::lock_guard<std::mutex> lock(configMutex);
  bCastConfig before = effective();
  change();
  if (!(effective() == before)) ++version;
}

bCastSnapshot bCast::snapshot() {
  std::lock_guard<std::mutex> lock(configMutex);
  return {effective(), managed.load(), version};
}

bCastConfig bCast::standaloneConfig() {
  std::lock_guard<std::mutex> lock(configMutex);
  return standalone;
}

void bCast::setStandalone(const std::function<void(bCastConfig&)>& edit) {
  update([&]() { edit(standalone); });
}

void bCast::setManaged(bool on) {
  update([&]() {
    if (on && !managed) {
      managedConfig.enabled = false;
      stats = {};
    }
    managed = on;
  });
}

void bCast::setManagedEnabled(bool enabled, const std::string& host, int port) {
  update([&]() {
    if (!managed) return;
    managedConfig.enabled = enabled;
    if (enabled) {
      managedConfig.host = host;
      managedConfig.port = port;
    }
  });
}

void bCast::setManagedPreset(int preset) {
  update([&]() {
    if (managed && bcast::isValidPreset(preset)) managedConfig.preset = preset;
  });
}

void bCast::setClientStats(const bCastClientStats& s) {
  std::lock_guard<std::mutex> lock(configMutex);
  stats = s;
  stats.valid = true;
}

bCastClientStats bCast::clientStats() {
  std::lock_guard<std::mutex> lock(configMutex);
  return stats;
}

json_t* bCast::dataToJson() {
  bCastConfig c = standaloneConfig();
  json_t* rootJ = json_object();
  json_object_set_new(rootJ, "dcFilter", json_boolean(dcFilterEnabled));
  json_object_set_new(rootJ, "enabled", json_boolean(c.enabled));
  json_object_set_new(rootJ, "preset", json_integer(c.preset));
  json_object_set_new(rootJ, "host", json_string(c.host.c_str()));
  json_object_set_new(rootJ, "port", json_integer(c.port));
  return rootJ;
}

void bCast::dataFromJson(json_t* rootJ) {
  if (json_t* j = json_object_get(rootJ, "dcFilter")) dcFilterEnabled = json_boolean_value(j);
  setStandalone([&](bCastConfig& c) {
    if (json_t* j = json_object_get(rootJ, "enabled")) c.enabled = json_boolean_value(j);
    if (json_t* j = json_object_get(rootJ, "preset")) {
      int preset = (int)json_integer_value(j);
      if (bcast::isValidPreset(preset)) c.preset = preset;
    }
    if (json_t* j = json_object_get(rootJ, "host")) {
      const char* host = json_string_value(j);
      if (host && *host) c.host = host;
    }
    if (json_t* j = json_object_get(rootJ, "port")) {
      int port = (int)json_integer_value(j);
      if (port > 0 && port < 65536) c.port = port;
    }
  });
}

namespace {

// commits on Enter, then closes the menu
struct ConfigField : ui::TextField {
  std::function<void(const std::string&)> commit;

  ConfigField() { box.size.x = 140; }

  void onAction(const ActionEvent& e) override {
    if (commit) commit(text);
    if (ui::MenuOverlay* overlay = getAncestorOfType<ui::MenuOverlay>())
      overlay->requestDelete();
    e.consume(this);
  }
};

}

bCastWidget::bCastWidget(bCast* module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/bCast.svg")));

  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
    Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)
  ));

  addChild(createLightCentered<MediumLight<RedGreenBlueLight>>(
    mm2px(Vec(12.7, 16)), module, bCast::STATUS_LIGHT
  ));

  for (int c = 0; c < 2; ++c) {
    float x = c == 0 ? 8.7f : 16.7f;
    int base = bCast::VU_LIGHTS + c * 6;
    addChild(createLightCentered<SmallLight<RedLight>>(mm2px(Vec(x, 26)), module, base));
    for (int row = 1; row <= 2; ++row)
      addChild(createLightCentered<SmallLight<YellowLight>>(
        mm2px(Vec(x, 26 + 5 * row)), module, base + row
      ));
    for (int row = 3; row < 6; ++row)
      addChild(createLightCentered<SmallLight<GreenLight>>(
        mm2px(Vec(x, 26 + 5 * row)), module, base + row
      ));
  }

  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.7, 66)), module, bCast::LEVEL_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 90)), module, bCast::LEFT_INPUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 106)), module, bCast::RIGHT_INPUT));
}

void bCastWidget::appendContextMenu(Menu* menu) {
  bCast* module = getModule<bCast>();
  if (!module) return;

  menu->addChild(new MenuSeparator);
  menu->addChild(createBoolPtrMenuItem("DC blocker", "", &module->dcFilterEnabled));
  menu->addChild(new MenuSeparator);

  if (module->isManaged()) {
    bCastSnapshot snap = module->snapshot();
    menu->addChild(createMenuLabel("Managed by OSCctrl"));
    menu->addChild(createMenuLabel(
      snap.config.enabled
        ? string::f("Streaming to %s:%d", snap.config.host.c_str(), snap.config.port)
        : "Not streaming"
    ));
    menu->addChild(createMenuLabel(
      string::f("Preset: %s", bcast::presetById(snap.config.preset).name)
    ));
    bCastClientStats s = module->clientStats();
    if (s.valid) {
      menu->addChild(createMenuLabel(string::f(
        "Client: %d underruns, %d lost, %d recovered", s.underruns, s.lost, s.recovered
      )));
      menu->addChild(createMenuLabel(string::f(
        "Jitter %.1f ms, buffer %.1f ms", s.jitterMs, s.bufferMs
      )));
    } else {
      menu->addChild(createMenuLabel("No client stats yet"));
    }
  } else {
    bCastConfig config = module->standaloneConfig();

    menu->addChild(createBoolMenuItem("Stream", "",
      [=]() { return module->standaloneConfig().enabled; },
      [=](bool on) { module->setStandalone([&](bCastConfig& c) { c.enabled = on; }); }
    ));

    std::vector<std::string> labels;
    for (const bcast::Preset& p : bcast::PRESETS) labels.push_back(p.name);
    menu->addChild(createIndexSubmenuItem("Preset", labels,
      [=]() { return (size_t)module->standaloneConfig().preset; },
      [=](size_t i) { module->setStandalone([&](bCastConfig& c) { c.preset = (int)i; }); }
    ));

    menu->addChild(createMenuLabel("Destination host (Enter to apply)"));
    ConfigField* hostField = new ConfigField;
    hostField->setText(config.host);
    hostField->commit = [=](const std::string& text) {
      std::string host = string::trim(text);
      if (host.empty()) return;
      module->setStandalone([&](bCastConfig& c) { c.host = host; });
    };
    menu->addChild(hostField);

    menu->addChild(createMenuLabel("Destination port (Enter to apply)"));
    ConfigField* portField = new ConfigField;
    portField->setText(std::to_string(config.port));
    portField->commit = [=](const std::string& text) {
      char* end = nullptr;
      long port = std::strtol(text.c_str(), &end, 10);
      if (end == text.c_str() || port <= 0 || port > 65535) return;
      module->setStandalone([&](bCastConfig& c) { c.port = (int)port; });
    };
    menu->addChild(portField);
  }

  if (uint32_t dropped = module->ringOverflows.load())
    menu->addChild(createMenuLabel(string::f("Dropped %u frames (ring full)", dropped)));
}

Model* modelbCast = createModel<bCast, bCastWidget>("bCast");
```

- [ ] **Step 5: Register the model**

`src/plugin.hpp`, after `extern Model* modelOSCctrl;`:

```cpp
extern Model* modelbCast;
```

`src/plugin.cpp`, after `p->addModel(modelOSCctrl);`:

```cpp
	p->addModel(modelbCast);
```

`plugin.json`: append to `"modules"`. Leave the user's `"version"` change
alone.

```json
    {
      "slug": "bCast",
      "name": "bCast",
      "description": "Streams stereo audio to a network client",
      "tags": ["External", "Utility"]
    }
```

- [ ] **Step 6: Build**

Run: `./build-wsl.sh -j8 2>&1 | tail -20`
Expected: links `plugin.so` with no errors or new warnings from `src/audio/`.

Then confirm the model is exported:
`nm -C plugin.so | grep -c modelbCast` (expected: at least 1).

- [ ] **Step 7: Hand off for a visual check**

The user builds with `./build-msys.sh -j8`, opens Rack, and checks:
- the panel shows the labels, status light, 2 × 6 VU, knob and jacks;
- a VCO into L lights both VU columns, and into R lights the right column only;
- the context menu shows DC blocker, Stream, Preset and the host/port
  fields, and a saved patch keeps preset, host and port after reopening;
- the status light stays off (the stub streamer never streams).

- [ ] **Step 8: Stop.** The user reviews and commits.

---
## Task 7: UDP socket and streamer thread

**Files:**
- Create: `src/audio/UdpAudioSocket.hpp`, `src/audio/UdpAudioSocket.cpp`
- Replace the Task 6 stubs: `src/audio/AudioStreamer.hpp`, `src/audio/AudioStreamer.cpp`

**Interfaces:**
- Consumes:
  - `AudioEncoder` (Task 3);
  - `PacketHeader`, `writePacket`, `maxFrameBytes`, `MAX_DATAGRAM`,
    `EncodedFrame` (Task 2);
  - from `bCast` (Task 6): `ring`, `streaming`, `engineSampleRate`, `epoch`,
    `sendFailing`, `snapshot()`, `streamId()`.
- Produces:
  - `bcast::UdpAudioSocket`: `open(host, port)`, `close()`,
    `send(data, size)`, `isOpen()`, `priorityMarked()`, `lastError()`;
  - `bcast::setHighResolutionTimer(bool)`;
  - `bcast::AudioStreamer(bCast*)`: `start()`, `stop()` (idempotent,
    joins).

Behaviour:
- One thread per module. It never takes the engine lock, so `onRemove` can
  join it while the engine holds its write lock.
- **Wakeups:** every 1 ms while streaming, every 20 ms while idle.
- **Reset:** when the config version or the engine rate changes. A reset
  stops pushes, clears the ring and buffers, zeroes seq, bumps the epoch and
  reopens. A failed open retries every second, with the light red meanwhile.
- **Send failures:** port-unreachable bounces (`ECONNREFUSED`,
  `WSAECONNRESET`) aren't failures; nobody listening is normal for
  standalone use. `sendFailing` turns on after 50 consecutive real failures.
- **Windows:** `sleep_for(1ms)` sleeps about 15.6 ms unless the process asks
  for 1 ms timer resolution. The streamer holds `timeBeginPeriod(1)` only
  while streaming.

- [ ] **Step 1: Write `src/audio/UdpAudioSocket.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace bcast {

// connected UDP socket for audio, marked for voice priority where the OS allows
class UdpAudioSocket {
public:
  UdpAudioSocket() = default;
  ~UdpAudioSocket() { close(); }
  UdpAudioSocket(const UdpAudioSocket&) = delete;
  UdpAudioSocket& operator=(const UdpAudioSocket&) = delete;

  bool open(const std::string& host, int port);
  void close();
  // false on a real failure. ICMP port-unreachable bounces count as success
  bool send(const uint8_t* data, size_t size);
  bool isOpen() const { return sock != INVALID; }
  bool priorityMarked() const { return marked; }
  const std::string& lastError() const { return error; }

private:
  static constexpr intptr_t INVALID = -1;
  intptr_t sock = INVALID;
  bool marked = false;
  void* qosHandle = nullptr;
  uint32_t qosFlow = 0;
  std::string error;

  void markPriority();
};

// 1 ms scheduler resolution on Windows; a no-op elsewhere. Calls must pair
void setHighResolutionTimer(bool on);

}
```

- [ ] **Step 2: Write `src/audio/UdpAudioSocket.cpp`**

```cpp
#ifdef _WIN32
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0601
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <mmsystem.h>
  #include <qos2.h>
  #ifndef SIO_UDP_CONNRESET
    #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
  #endif
#else
  #include <cerrno>
  #include <cstring>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/ip.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include "audio/UdpAudioSocket.hpp"

namespace bcast {

namespace {

#ifdef _WIN32
using Native = SOCKET;
const Native NATIVE_INVALID = INVALID_SOCKET;

void ensureNetworking() {
  struct Init {
    Init() { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); }
    ~Init() { WSACleanup(); }
  };
  static Init init;
}

void closeNative(Native s) { closesocket(s); }
std::string lastSocketError() { return "error " + std::to_string(WSAGetLastError()); }
#else
using Native = int;
const Native NATIVE_INVALID = -1;

void ensureNetworking() {}
void closeNative(Native s) { ::close(s); }
std::string lastSocketError() { return std::strerror(errno); }
#endif

Native native(intptr_t s) { return (Native)s; }

}

bool UdpAudioSocket::open(const std::string& host, int port) {
  close();
  ensureNetworking();
  error.clear();

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  addrinfo* found = nullptr;
  std::string service = std::to_string(port);
  if (getaddrinfo(host.c_str(), service.c_str(), &hints, &found) != 0 || !found) {
    error = "can't resolve " + host;
    return false;
  }

  Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == NATIVE_INVALID) {
    error = "socket: " + lastSocketError();
    freeaddrinfo(found);
    return false;
  }
  if (::connect(s, found->ai_addr, (int)found->ai_addrlen) != 0) {
    error = "connect: " + lastSocketError();
    closeNative(s);
    freeaddrinfo(found);
    return false;
  }
  freeaddrinfo(found);

  sock = (intptr_t)s;
  markPriority();
  return true;
}

void UdpAudioSocket::markPriority() {
  Native s = native(sock);
#ifdef _WIN32
  // otherwise an ICMP port-unreachable makes later sends fail
  BOOL report = FALSE;
  DWORD bytes = 0;
  WSAIoctl(s, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &bytes, nullptr, nullptr);

  // Windows ignores IP_TOS; qWAVE marks voice traffic (DSCP 46, WMM AC_VO)
  QOS_VERSION version{1, 0};
  HANDLE handle = nullptr;
  if (QOSCreateHandle(&version, &handle)) {
    QOS_FLOWID flow = 0;
    if (QOSAddSocketToFlow(handle, s, nullptr, QOSTrafficTypeVoice, QOS_NON_ADAPTIVE_FLOW, &flow)) {
      qosHandle = handle;
      qosFlow = flow;
      marked = true;
    } else {
      QOSCloseHandle(handle);
    }
  }
#else
  int tos = 0xB8;  // DSCP EF (46)
  marked = setsockopt(s, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) == 0;
  #ifdef __linux__
  int priority = 6;  // the highest without CAP_NET_ADMIN; maps to WMM voice
  setsockopt(s, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
  #endif
#endif
}

void UdpAudioSocket::close() {
  if (sock == INVALID) return;
#ifdef _WIN32
  if (qosHandle) {
    QOSRemoveSocketFromFlow((HANDLE)qosHandle, native(sock), qosFlow, 0);
    QOSCloseHandle((HANDLE)qosHandle);
    qosHandle = nullptr;
    qosFlow = 0;
  }
#endif
  closeNative(native(sock));
  sock = INVALID;
  marked = false;
}

bool UdpAudioSocket::send(const uint8_t* data, size_t size) {
  if (sock == INVALID) return false;
#ifdef _WIN32
  int sent = ::send(native(sock), (const char*)data, (int)size, 0);
  if (sent == SOCKET_ERROR) {
    int e = WSAGetLastError();
    return e == WSAECONNRESET || e == WSAECONNREFUSED;
  }
#else
  ssize_t sent = ::send(native(sock), data, size, 0);
  if (sent < 0) return errno == ECONNREFUSED || errno == EINTR;
#endif
  return true;
}

void setHighResolutionTimer(bool on) {
#ifdef _WIN32
  if (on) timeBeginPeriod(1);
  else timeEndPeriod(1);
#else
  (void)on;
#endif
}

}
```

- [ ] **Step 3: Replace `src/audio/AudioStreamer.hpp`**

```cpp
#pragma once
#include "plugin.hpp"
#include "audio/AudioEncoder.hpp"
#include "audio/Packet.hpp"
#include "audio/Presets.hpp"
#include "audio/UdpAudioSocket.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <string>
#include <thread>
#include <vector>

struct bCast;
struct bCastConfig;

namespace bcast {

// Drains the module's ring, resamples to 48 kHz, frames, encodes and sends.
// Owned by the module; never touches the engine.
class AudioStreamer {
public:
  explicit AudioStreamer(bCast* module) : module(module) {}
  ~AudioStreamer() { stop(); }
  AudioStreamer(const AudioStreamer&) = delete;
  AudioStreamer& operator=(const AudioStreamer&) = delete;

  void start();
  void stop();

private:
  using clock = std::chrono::steady_clock;
  static constexpr int FAILING_AFTER = 50;

  bCast* module;
  std::atomic<bool> running{false};
  std::thread thread;

  // streamer-thread state
  Preset preset = presetById(DEFAULT_PRESET);
  std::string host;
  int port = 0;
  bool wantOpen = false;
  bool openWarned = false;
  bool highResTimer = false;
  clock::time_point lastOpenAttempt;
  int inRate = 0;
  uint32_t streamId = 0;
  uint16_t epoch = 0;
  uint32_t seq = 0;
  int sendFailures = 0;

  AudioEncoder encoder;
  UdpAudioSocket socket;
  rack::dsp::SampleRateConverter<2> src;
  std::vector<rack::dsp::Frame<2>> pending;    // engine rate, not yet resampled
  std::vector<rack::dsp::Frame<2>> resampled;  // scratch
  std::vector<float> accum;                    // 48 kHz interleaved, not yet framed
  std::deque<EncodedFrame> history;            // newest first, consecutive seqs
  std::vector<uint8_t> datagram;

  void run();
  void reset(const bCastConfig& config, int rate);
  void setStreaming(bool on);
  bool openSocket();
  void pump();
  void emitFrame(const float* pcm);
};

}
```

- [ ] **Step 4: Replace `src/audio/AudioStreamer.cpp`**

```cpp
#include "audio/AudioStreamer.hpp"
#include "audio/bCast.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bcast {

void AudioStreamer::start() {
  if (running.exchange(true)) return;
  thread = std::thread([this]() { run(); });
}

void AudioStreamer::stop() {
  running = false;
  if (thread.joinable()) thread.join();
}

void AudioStreamer::run() {
  uint64_t seenVersion = std::numeric_limits<uint64_t>::max();
  int seenRate = -1;

  while (running.load()) {
    bCastSnapshot snap = module->snapshot();
    int rate = (int)std::lround(module->engineSampleRate.load());

    if (snap.version != seenVersion || rate != seenRate) {
      seenVersion = snap.version;
      seenRate = rate;
      reset(snap.config, rate);
    } else if (
      wantOpen && !socket.isOpen()
        && clock::now() - lastOpenAttempt >= std::chrono::seconds(1)
    ) {
      if (openSocket()) setStreaming(true);
    }

    if (module->streaming.load()) {
      pump();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  setStreaming(false);
  socket.close();
}

void AudioStreamer::setStreaming(bool on) {
  module->streaming = on;
  if (on != highResTimer) {
    setHighResolutionTimer(on);
    highResTimer = on;
  }
}

void AudioStreamer::reset(const bCastConfig& config, int rate) {
  // process() may land one more frame after the clear; it's harmless
  setStreaming(false);
  module->ring.clear();
  pending.clear();
  accum.clear();
  history.clear();
  seq = 0;
  epoch = ++module->epoch;
  sendFailures = 0;
  module->sendFailing = false;
  socket.close();

  streamId = module->streamId();
  preset = presetById(config.preset);
  host = config.host;
  port = config.port;
  inRate = rate;
  openWarned = false;
  wantOpen = config.enabled && rate > 0;
  if (!wantOpen) return;

  if (!encoder.reset(preset)) {
    WARN("bCast %u: can't create the encoder for preset %d", streamId, preset.id);
    module->sendFailing = true;
    wantOpen = false;
    return;
  }
  src.setChannels(2);
  src.setRates(inRate, SAMPLE_RATE);
  src.refreshState();  // drop filter history from the previous epoch

  if (openSocket()) setStreaming(true);
}

bool AudioStreamer::openSocket() {
  lastOpenAttempt = clock::now();
  if (socket.open(host, port)) {
    INFO(
      "bCast %u: streaming %s to %s:%d, epoch %u%s",
      streamId, preset.name, host.c_str(), port, (unsigned)epoch,
      socket.priorityMarked() ? "" : " (no priority marking)"
    );
    module->sendFailing = false;
    openWarned = false;
    return true;
  }
  if (!openWarned) {
    WARN("bCast %u: can't open %s:%d: %s", streamId, host.c_str(), port, socket.lastError().c_str());
    openWarned = true;
  }
  module->sendFailing = true;
  return false;
}

void AudioStreamer::pump() {
  rack::dsp::Frame<2> chunk[256];
  while (size_t n = std::min<size_t>(module->ring.size(), 256)) {
    module->ring.shiftBuffer(chunk, n);
    pending.insert(pending.end(), chunk, chunk + n);
  }
  if (pending.empty()) return;

  int inFrames = (int)pending.size();
  int outFrames = (int)((int64_t)inFrames * SAMPLE_RATE / inRate) + 64;
  resampled.resize(outFrames);
  src.process(pending.data(), &inFrames, resampled.data(), &outFrames);
  // speex may leave input it couldn't fit; keep it for the next pass
  pending.erase(pending.begin(), pending.begin() + inFrames);

  const float* out = (const float*)resampled.data();
  accum.insert(accum.end(), out, out + (size_t)outFrames * CHANNELS);

  const size_t frameFloats = (size_t)preset.frameSamples * CHANNELS;
  size_t offset = 0;
  while (accum.size() - offset >= frameFloats) {
    emitFrame(&accum[offset]);
    offset += frameFloats;
  }
  accum.erase(accum.begin(), accum.begin() + offset);
}

void AudioStreamer::emitFrame(const float* pcm) {
  EncodedFrame frame(maxFrameBytes(1 + preset.redundancy));
  int bytes = encoder.encode(pcm, frame.data(), (int)frame.size());
  if (bytes < 0) {
    // the seq still advances, so the client sees the gap and conceals it
    WARN("bCast %u: encode failed at seq %u", streamId, seq);
    history.clear();
    ++seq;
    return;
  }
  frame.resize(bytes);
  history.push_front(std::move(frame));
  while ((int)history.size() > 1 + preset.redundancy) history.pop_back();

  std::vector<const EncodedFrame*> frames;
  for (const EncodedFrame& f : history) frames.push_back(&f);

  PacketHeader header{
    streamId, epoch, preset.codec,
    (uint16_t)preset.frameSamples, (uint8_t)preset.id, seq,
  };
  datagram.resize(MAX_DATAGRAM);
  size_t size = writePacket(datagram.data(), datagram.size(), header, frames);
  ++seq;
  if (size == 0) {
    // can't happen while frames are capped at maxFrameBytes
    history.clear();
    return;
  }

  bool ok = socket.send(datagram.data(), size);
  sendFailures = ok ? 0 : sendFailures + 1;
  module->sendFailing = sendFailures >= FAILING_AFTER;
}

}
```

- [ ] **Step 5: Build and run the unit tests**

Run: `./build-wsl.sh -j8 2>&1 | tail -20 && tests/run.sh`
Expected:
- the plugin builds with no errors or new warnings;
- `7 tests, 0 failed checks`, since the harness doesn't compile the
  streamer.

Then the Windows build: `./build-msys.sh -j8` has to compile and link
`UdpAudioSocket.cpp` against `-lqwave -lwinmm`. If an agent can't run MSYS,
the user runs it. If `qos2.h` or `libqwave.a` is missing from the
toolchain, guard the qWAVE block with `#if __has_include(<qos2.h>)`, drop
`-lqwave`, and note that Windows sends go unmarked.

- [ ] **Step 6: Live check with a standalone bCast (the user runs it)**

The user runs Rack on Windows, so this step runs `audio_bench.py`
natively on Windows (see `tools/bench/AGENTS.md`):
1. Patch Fundamental VCO SIN into bCast L, level at 0 dB. Set the
   destination host to `127.0.0.1` and the port to 7747, and turn on
   Stream. The status light goes green.
2. For each preset (switch it from the bCast menu between runs):
   `python audio_bench.py run --passive --preset ID --duration 10 --analyze --wav preset-ID.wav --json passive-ID.json`.
3. Expected for every preset:
   - 0 lost, 0 reordered, 0 duplicates;
   - packet rate 1000 / frame ms (200/s for presets 0 and 3, 100/s for 1,
     50/s for 2);
   - drift within ±200 ppm (the engine clock against the client's clock);
   - sine at the VCO frequency with 0 dropouts. Glitches: 0 for preset 3
     (PCM). For Opus, any glitch should be explained by listening to the
     `.wav`;
   - no `bCast ... can't open` lines in Rack's `log.txt`, plus one
     `streaming ... epoch N` line per preset switch.
4. Change the engine sample rate (for example to 96 kHz). The epoch goes up,
   and a fresh passive run passes the same checks.
5. Set the host to `nonexistent.invalid`. The light goes red and `log.txt`
   gets one `can't open` warning (not one per second). Set it back to
   `127.0.0.1` and the light is green within about a second.
6. Delete the module while it streams. Rack doesn't hang, and packets stop.

- [ ] **Step 7: Stop.** The user reviews and commits.

---
## Task 8: ChunkPacer

**Files:**
- Create: `src/osc/ChunkPacer.hpp`, `src/osc/ChunkPacer.cpp`
- Test: `tests/test_pacer.cpp`

**Interfaces:**
- Produces `ChunkPacer` (global namespace, Rack-free, internally locked):
  - `ChunkPacer::clock` (`steady_clock`) and `ChunkPacer::AudioStats`
    `{int underruns, lost, recovered; float jitterMs, bufferMs}`;
  - `std::atomic<bool> enabled{true}`;
  - `setActive(bool, now)`: start or stop pacing for an audio stream. Both
    reset to unlimited;
  - `onStats(AudioStats, now)`: AIMD step from cumulative counts;
  - `nextSendTime(now)`: `time_point::min()` if a chunk may go now,
    otherwise when it may;
  - `consume(bytes, now)`: charge the bucket for a sent chunk;
  - `rateMbps(now)`: the current limit, 0 when unlimited.

Rules (from Global Constraints):
- Unlimited while inactive, before the first trouble, and when stats go
  stale (2 s without any).
- **Trouble:** a new underrun, a new loss, or jitter above
  `max(1.5 × min, min + 2 ms)`, where `min` is the lowest jitter reported in
  the last 10 s. On trouble the rate is ×0.7, floored at 20 Mbit/s. The
  first decrease from unlimited starts from 400.
- **Clean report:** the rate gets +10 Mbit/s if limited. Reaching 400 makes
  it unlimited.
- **Counters going backwards** (the client restarted its stream): take a new
  baseline without adjusting.
- **Bucket:** burst `8 × 1452` bytes, refilled at the rate, and allowed to go
  negative. A chunk may go while the balance is above 0.

- [ ] **Step 1: Write the failing tests**

`tests/test_pacer.cpp`:

```cpp
#include "test.hpp"
#include "osc/ChunkPacer.hpp"

#include <cmath>

using clk = ChunkPacer::clock;
using std::chrono::milliseconds;
using std::chrono::microseconds;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

static ChunkPacer::AudioStats stats(int underruns, int lost = 0, float jitter = 1.f) {
  return {underruns, lost, 0, jitter, 40.f};
}

// active, baselined at t0 + 500 ms, then one underrun at t0 + 1 s: 280 Mbit/s
static clk::time_point limited(ChunkPacer& p, clk::time_point t0) {
  p.setActive(true, t0);
  p.onStats(stats(0), t0 + milliseconds(500));
  p.onStats(stats(1), t0 + milliseconds(1000));
  return t0 + milliseconds(1000);
}

TEST(pacer_unlimited_at_start) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  CHECK(p.nextSendTime(t0) == clk::time_point::min());
  CHECK(near(p.rateMbps(t0), 0));
  p.setActive(true, t0);
  for (int i = 0; i < 100; ++i) p.consume(1452, t0);
  CHECK(p.nextSendTime(t0) == clk::time_point::min());
}

TEST(pacer_decreases_on_underrun_and_loss) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  CHECK(near(p.rateMbps(t), 280));
  p.onStats(stats(1, 2), t + milliseconds(500));
  CHECK(near(p.rateMbps(t + milliseconds(500)), 196));
}

TEST(pacer_floor) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 2; i < 20; ++i) {
    t += milliseconds(500);
    p.onStats(stats(i), t);
  }
  CHECK(near(p.rateMbps(t), 20));
}

TEST(pacer_additive_increase_reaches_unlimited) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 0; i < 11; ++i) {
    t += milliseconds(500);
    p.onStats(stats(1), t);
  }
  CHECK(near(p.rateMbps(t), 390));
  t += milliseconds(500);
  p.onStats(stats(1), t);
  CHECK(near(p.rateMbps(t), 0));
}

TEST(pacer_stale_stats_unlimit) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  CHECK(near(p.rateMbps(t + milliseconds(1900)), 280));
  CHECK(near(p.rateMbps(t + milliseconds(2100)), 0));
  // no backlog of waiting once unlimited
  for (int i = 0; i < 50; ++i) p.consume(1452, t + milliseconds(2100));
  CHECK(p.nextSendTime(t + milliseconds(2100)) == clk::time_point::min());
}

TEST(pacer_burst_then_wait) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 0; i < 8; ++i) {
    CHECK(p.nextSendTime(t) == clk::time_point::min());
    p.consume(1452, t);
  }
  CHECK(p.nextSendTime(t) != clk::time_point::min());
}

TEST(pacer_wait_time) {
  ChunkPacer p;
  clk::time_point t = limited(p, clk::now());
  for (int i = 0; i < 9; ++i) p.consume(1452, t);
  // 1452 bytes behind at 280 Mbit/s = 35 MB/s: about 41.5 us
  clk::time_point at = p.nextSendTime(t);
  long us = (long)std::chrono::duration_cast<microseconds>(at - t).count();
  CHECK(us >= 40 && us <= 43);
  CHECK(p.nextSendTime(t + microseconds(50)) == clk::time_point::min());
}

TEST(pacer_jitter_trouble) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  p.setActive(true, t0);
  clk::time_point t = t0;
  for (int i = 0; i < 4; ++i) {
    t += milliseconds(500);
    p.onStats(stats(0, 0, 1.f), t);
  }
  t += milliseconds(500);
  p.onStats(stats(0, 0, 2.9f), t);  // under max(1.5, 3.0)
  CHECK(near(p.rateMbps(t), 0));
  t += milliseconds(500);
  p.onStats(stats(0, 0, 5.f), t);
  CHECK(near(p.rateMbps(t), 280));
}

TEST(pacer_rebaselines_when_counters_drop) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  p.setActive(true, t0);
  p.onStats(stats(5, 3), t0 + milliseconds(500));
  p.onStats(stats(0, 0), t0 + milliseconds(1000));
  CHECK(near(p.rateMbps(t0 + milliseconds(1000)), 0));
  p.onStats(stats(0, 0), t0 + milliseconds(1500));
  CHECK(near(p.rateMbps(t0 + milliseconds(1500)), 0));
  p.onStats(stats(1, 0), t0 + milliseconds(2000));
  CHECK(near(p.rateMbps(t0 + milliseconds(2000)), 280));
}

TEST(pacer_inactive_or_disabled_is_unlimited) {
  ChunkPacer p;
  clk::time_point t0 = clk::now();
  p.onStats(stats(0), t0);
  p.onStats(stats(3), t0 + milliseconds(500));
  CHECK(near(p.rateMbps(t0 + milliseconds(500)), 0));

  clk::time_point t = limited(p, t0 + milliseconds(1000));
  for (int i = 0; i < 9; ++i) p.consume(1452, t);
  p.enabled = false;
  CHECK(p.nextSendTime(t) == clk::time_point::min());
  p.enabled = true;
  CHECK(p.nextSendTime(t) != clk::time_point::min());
  p.setActive(false, t);
  CHECK(near(p.rateMbps(t), 0));
  CHECK(p.nextSendTime(t) == clk::time_point::min());
}
```

- [ ] **Step 2: Run the tests and confirm they fail**

Run: `tests/run.sh pacer`
Expected: compile error, `osc/ChunkPacer.hpp: No such file or directory`.

- [ ] **Step 3: Write `src/osc/ChunkPacer.hpp`**

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

// Paces texture chunk sends while an audio stream plays, so overlay bursts
// don't fill the Wi-Fi queues ahead of audio. A token bucket whose rate is
// steered by the client's audio stats (AIMD). Thread-safe.
class ChunkPacer {
public:
  using clock = std::chrono::steady_clock;

  struct AudioStats {
    int underruns = 0;
    int lost = 0;
    int recovered = 0;
    float jitterMs = 0.f;
    float bufferMs = 0.f;
  };

  static constexpr double BURST_BYTES = 8 * 1452;
  static constexpr double MAX_MBPS = 400;
  static constexpr double MIN_MBPS = 20;
  static constexpr double INCREASE_MBPS = 10;
  static constexpr double DECREASE = 0.7;
  static constexpr clock::duration STATS_TIMEOUT = std::chrono::seconds(2);
  static constexpr clock::duration JITTER_WINDOW = std::chrono::seconds(10);

  // bench A/B toggle
  std::atomic<bool> enabled{true};

  void setActive(bool active, clock::time_point now);
  void onStats(const AudioStats& stats, clock::time_point now);
  clock::time_point nextSendTime(clock::time_point now);
  void consume(size_t bytes, clock::time_point now);
  double rateMbps(clock::time_point now);

private:
  std::mutex mutex;
  bool active = false;
  double rate = 0;  // Mbit/s, 0 = unlimited
  double tokens = BURST_BYTES;
  clock::time_point lastRefill{};
  clock::time_point lastStats{};
  bool haveBaseline = false;
  AudioStats baseline;
  std::deque<std::pair<clock::time_point, float>> jitter;

  void expireIfStale(clock::time_point now);
  void refill(clock::time_point now);
  bool jitterTrouble(float jitterMs, clock::time_point now);
};
```

- [ ] **Step 4: Write `src/osc/ChunkPacer.cpp`**

```cpp
#include "ChunkPacer.hpp"

#include <algorithm>

void ChunkPacer::setActive(bool on, clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  active = on;
  rate = 0;
  tokens = BURST_BYTES;
  lastRefill = now;
  lastStats = now;
  haveBaseline = false;
  jitter.clear();
}

void ChunkPacer::expireIfStale(clock::time_point now) {
  if (rate > 0 && now - lastStats >= STATS_TIMEOUT) rate = 0;
}

void ChunkPacer::refill(clock::time_point now) {
  if (rate <= 0) {
    tokens = BURST_BYTES;
  } else if (now > lastRefill) {
    double sec = std::chrono::duration<double>(now - lastRefill).count();
    tokens = std::min(BURST_BYTES, tokens + sec * rate * 1e6 / 8);
  }
  lastRefill = std::max(lastRefill, now);
}

bool ChunkPacer::jitterTrouble(float jitterMs, clock::time_point now) {
  jitter.emplace_back(now, jitterMs);
  while (!jitter.empty() && now - jitter.front().first > JITTER_WINDOW) jitter.pop_front();
  float low = jitterMs;
  for (const auto& [at, value] : jitter) low = std::min(low, value);
  return jitterMs > std::max(1.5f * low, low + 2.f);
}

void ChunkPacer::onStats(const AudioStats& stats, clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return;
  expireIfStale(now);
  lastStats = now;

  bool jittery = jitterTrouble(stats.jitterMs, now);
  if (
    !haveBaseline
      || stats.underruns < baseline.underruns
      || stats.lost < baseline.lost
  ) {
    baseline = stats;
    haveBaseline = true;
    return;
  }

  bool trouble = jittery
    || stats.underruns > baseline.underruns
    || stats.lost > baseline.lost;
  baseline = stats;

  refill(now);
  if (trouble) {
    rate = std::max(MIN_MBPS, (rate > 0 ? rate : MAX_MBPS) * DECREASE);
  } else if (rate > 0) {
    rate += INCREASE_MBPS;
    if (rate >= MAX_MBPS - 1e-6) rate = 0;
  }
}

ChunkPacer::clock::time_point ChunkPacer::nextSendTime(clock::time_point now) {
  if (!enabled.load(std::memory_order_relaxed)) return clock::time_point::min();
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return clock::time_point::min();
  expireIfStale(now);
  refill(now);
  if (rate <= 0 || tokens > 0) return clock::time_point::min();

  double bytesPerSec = rate * 1e6 / 8;
  double sec = std::max(-tokens, 1.0) / bytesPerSec;
  return now + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(sec));
}

void ChunkPacer::consume(size_t bytes, clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return;
  expireIfStale(now);
  refill(now);
  if (rate > 0) tokens -= (double)bytes;
}

double ChunkPacer::rateMbps(clock::time_point now) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!active) return 0;
  expireIfStale(now);
  return rate;
}
```

- [ ] **Step 5: Run the tests and confirm they pass**

Run: `tests/run.sh pacer && tests/run.sh`
Expected: 10 pacer `PASS` lines, then `17 tests, 0 failed checks` for the
whole suite.

In `pacer_jitter_trouble` the four 1 ms reports set the 10 s minimum, so
2.9 ms passes (the limit is 3.0) and 5 ms is trouble.

- [ ] **Step 6: Stop.** The user reviews and commits.

---

## Task 9: OSCctrl integration

**Files:**
- Create: `src/osc/Bundler/AudioBundlers.hpp`, `src/osc/Bundler/AudioBundlers.cpp`
- Modify: `src/osc/OscSender.hpp`, `src/osc/OscSender.cpp`
- Modify: `src/osc/OscReceiver.cpp`
- Modify: `src/OSCctrl.hpp`, `src/OSCctrl.cpp`
- Modify: `API.md`

**Interfaces:**
- Consumes:
  - `ChunkPacer` (Task 8);
  - from `bCast` (Task 6): `setManaged`, `setManagedEnabled`,
    `setManagedPreset`, `setClientStats`, `snapshot`, `streamId`, `epoch`;
  - `modelbCast`;
  - the Python client from Task 5, for verification.
- Produces:
  - `OscSender::pacer` and `OscSender::directHost()`;
  - `AudioStreamBundler` and `AudioAckBundler`;
  - on `OSCctrlWidget`: `managedAudioId`, `managedAudio()`,
    `stepAudioExpander()`, `announceAudioStream()`, `onClientRegistered()`,
    `onClientGone()`;
  - routes `/set/audio/enabled`, `/set/audio/preset`, `/audio/stats` and
    (BENCH) `/bench/chunk_pacing`;
  - bench metrics `tx.pacing.wait` (sample, ms), `tx.pacing.rate_mbps`
    (gauge), `audio.client.underruns` and `audio.client.lost` (gauges),
    `audio.client.jitter_ms` (sample).

Threading:
- Everything that touches a `bCast` runs on the UI thread through
  `enqueueAction`, the same as the param routes. `APP->engine->getModule`
  finds the module by id, so no thread holds a raw expander pointer across
  frames.
- `/register` arrives on the receive thread and client-gone on the
  heartbeat timer thread. Both enqueue an action.
- The pacer lives in `OscSender`. Only the queue worker waits on it, under
  `qmutex`. The pacer has its own mutex and never takes `qmutex`.

- [ ] **Step 1: Write the bundlers**

`src/osc/Bundler/AudioBundlers.hpp`:

```cpp
#pragma once

#include "Bundler.hpp"

// /set/audio_stream: the bCast expander's stream, for the client to play
struct AudioStreamBundler : Bundler {
  AudioStreamBundler(
    int64_t moduleId,
    uint32_t streamId,
    int32_t presetId,
    bool enabled,
    uint16_t epoch
  );
};

// /set/audio/enabled/ack and /set/audio/preset/ack
struct AudioAckBundler : Bundler {
  AudioAckBundler(const std::string& address, int64_t moduleId, int32_t value, bool success);
};
```

`src/osc/Bundler/AudioBundlers.cpp`:

```cpp
#include "AudioBundlers.hpp"

AudioStreamBundler::AudioStreamBundler(
  int64_t moduleId,
  uint32_t streamId,
  int32_t presetId,
  bool enabled,
  uint16_t epoch
): Bundler("AudioStreamBundler") {
  messages.emplace_back(
    "/set/audio_stream",
    [=](osc::OutboundPacketStream& pstream) {
      // OSC has no unsigned int; the client reads streamId back as uint32
      pstream << moduleId
        << (int32_t)streamId
        << presetId
        << (int32_t)enabled
        << (int32_t)epoch;
    }
  );
}

AudioAckBundler::AudioAckBundler(
  const std::string& address,
  int64_t moduleId,
  int32_t value,
  bool success
): Bundler("AudioAckBundler") {
  messages.emplace_back(
    address,
    [=](osc::OutboundPacketStream& pstream) {
      pstream << moduleId << value << (int32_t)success;
    }
  );
}
```

The Makefile's sources wildcard has to include `src/osc/Bundler/*.cpp`.
Check with `grep -n "SOURCES" Makefile`. It already compiles
`ParamAckBundler.cpp`, so it should.

- [ ] **Step 2: Add the pacer and `directHost()` to `OscSender`**

`src/osc/OscSender.hpp`: add `#include "ChunkPacer.hpp"` and `#include <string>`
after `#include "OscConstants.hpp"`. In the public part, after
`void setDirect(char* ip);`, add:

```cpp
  // paces image chunks while an audio stream plays
  ChunkPacer pacer;
  // the registered client's address
  std::string directHost();
```

`src/osc/OscSender.cpp`: add after `OscSender::setDirect`:

```cpp
std::string OscSender::directHost() {
  std::unique_lock<std::mutex> locker(qmutex);
  char ip[IpEndpointName::ADDRESS_STRING_LENGTH];
  directEndpoint.AddressAsString(ip);
  return ip;
}
```

In `processQueue`, replace:

```cpp
    if (!bundler && !chunkQueue.empty()) {
      bundler = chunkQueue.front().bundler;
```

with:

```cpp
    if (!bundler && !chunkQueue.empty()) {
      auto now = std::chrono::steady_clock::now();
      auto sendAt = pacer.nextSendTime(now);
      if (sendAt != ChunkPacer::clock::time_point::min()) {
        // anything but chunks wakes us early; chunks wait their turn
        queueLockCondition.wait_until(locker, sendAt, [this]() {
          return !queueWorkerRunning
            || !priorityQueue.empty()
            || lightsMailbox.load() != nullptr
            || heartbeatMailbox.load() != nullptr;
        });
        BENCH(bench::sample(
          "tx.pacing.wait",
          std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - now
          ).count()
        );)
        continue;
      }
      bundler = chunkQueue.front().bundler;
```

In the send loop, count the bytes. Replace:

```cpp
    if (!bundler->isNoop()) {
      while (bundler->hasRemainingMessages()) {
```

with:

```cpp
    size_t sentBytes = 0;
    if (!bundler->isNoop()) {
      while (bundler->hasRemainingMessages()) {
```

and replace:

```cpp
        sendBundle(pstream);
      }

      bundler->sent();
```

with:

```cpp
        sendBundle(pstream);
        sentBytes += pstream.Size();
      }

      bundler->sent();
```

At the end of the loop body, charge the bucket for chunks. Replace:

```cpp
    if (!isChunk) continue;
    int64_t elapsed =
```

with:

```cpp
    if (!isChunk) continue;
    pacer.consume(sentBytes, std::chrono::steady_clock::now());
    int64_t elapsed =
```

`continue` drops `locker` at the end of the iteration and goes back to the
top. The wait predicate there is already true because the chunk is still
queued, so the loop re-checks the mailboxes and the pacer.
`stopQueueWorker` enqueues a noop priority bundler, which also ends a pacing
wait.

- [ ] **Step 3: Add the expander management to `OSCctrlWidget`**

`src/OSCctrl.hpp`: add `struct bCast;` next to the other forward
declarations. In `struct OSCctrlWidget`, after `void appendContextMenu(Menu* menu) override;`,
add:

```cpp
  // a bCast directly left or right of the primary OSCctrl. UI thread only
  int64_t managedAudioId = -1;
  bCast* managedAudio();
  void stepAudioExpander();
  // /set/audio_stream for the managed bCast, when a client is registered
  void announceAudioStream();
  // UI thread: a client registered, or stopped answering heartbeats
  void onClientRegistered();
  void onClientGone();
```

`src/OSCctrl.cpp`: add includes:

```cpp
#include "audio/bCast.hpp"
#include "osc/Bundler/AudioBundlers.hpp"
```

In `OSCctrlWidget::step()`, after `processActionQueue();`:

```cpp
  if (osctx) stepAudioExpander();
```

At the top of `OSCctrlWidget::~OSCctrlWidget()`, after
`bool isPrimaryOSCctrl = osctx != NULL;`:

```cpp
  // hand the expander back to its own settings
  if (isPrimaryOSCctrl) {
    if (bCast* audio = managedAudio()) audio->setManaged(false);
  }
```

Add, after `OSCctrlWidget::step()`:

```cpp
bCast* OSCctrlWidget::managedAudio() {
  if (managedAudioId < 0) return nullptr;
  Module* m = APP->engine->getModule(managedAudioId);
  return (m && m->model == modelbCast) ? static_cast<bCast*>(m) : nullptr;
}

void OSCctrlWidget::stepAudioExpander() {
  int64_t foundId = -1;
  for (int64_t id : {module->leftExpander.moduleId, module->rightExpander.moduleId}) {
    if (id < 0) continue;
    Module* m = APP->engine->getModule(id);
    if (m && m->model == modelbCast) {
      foundId = id;
      break;
    }
  }
  if (foundId == managedAudioId) return;

  if (managedAudioId >= 0) {
    if (bCast* old = managedAudio()) old->setManaged(false);
    osctx->pacer.setActive(false, std::chrono::steady_clock::now());
    if (!osctx->isBroadcasting())
      osctx->enqueueBundler(new AudioStreamBundler(
        managedAudioId, (uint32_t)managedAudioId, bcast::DEFAULT_PRESET, false, 0
      ));
  }

  managedAudioId = foundId;
  if (bCast* audio = managedAudio()) {
    audio->setManaged(true);
    announceAudioStream();
  }
}

void OSCctrlWidget::announceAudioStream() {
  if (!osctx || osctx->isBroadcasting()) return;
  bCast* audio = managedAudio();
  if (!audio) return;
  bCastSnapshot snap = audio->snapshot();
  osctx->enqueueBundler(new AudioStreamBundler(
    audio->id,
    audio->streamId(),
    snap.config.preset,
    snap.config.enabled,
    audio->epoch.load()
  ));
}

void OSCctrlWidget::onClientRegistered() {
  if (bCast* audio = managedAudio()) audio->setManagedEnabled(false, "", 0);
  osctx->pacer.setActive(false, std::chrono::steady_clock::now());
  announceAudioStream();
}

void OSCctrlWidget::onClientGone() {
  if (bCast* audio = managedAudio()) audio->setManagedEnabled(false, "", 0);
  osctx->pacer.setActive(false, std::chrono::steady_clock::now());
}
```

The epoch in `/set/audio_stream` can lag by one: the streamer bumps it on
its next wakeup after a config change. The packets carry the real epoch,
and both clients key their resets on the packets.

- [ ] **Step 4: Add the routes to `OscReceiver`**

`src/osc/OscReceiver.cpp`: add includes after `#include "../texture/Renderer.hpp"`:

```cpp
#include "../audio/bCast.hpp"
#include "Bundler/AudioBundlers.hpp"
```

In the `BENCH(...)` block of `generateRoutes`, after the `/bench/supersede`
route:

```cpp

    routes.emplace(
      "/bench/chunk_pacing",
      [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
        bool enabled = (args++)->AsInt32() != 0;
        ctrl->enqueueAction([this, enabled]() {
          osctx->pacer.enabled = enabled;
          osctx->enqueueBundler(
            new BenchToggleAckBundler("/bench/chunk_pacing", enabled)
          );
        }, "bench");
      }
    );
```

In `/register`, after the heartbeat `enqueueAction`:

```cpp

      ctrl->enqueueAction([this]() { ctrl->onClientRegistered(); }, "audio");
```

In the heartbeat timer's client-gone branch, after
`ModuleParamsBundler::params.clear();`:

```cpp

      ctrl->enqueueAction([this]() { ctrl->onClientGone(); }, "audio");
```

After the `/set/param/value` route:

```cpp

  routes.emplace(
    "/set/audio/enabled",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();
      bool enabled = (args++)->AsInt32() != 0;
      int32_t port = (args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        bCast* audio = ctrl->managedAudio();
        bool ok = audio && audio->id == moduleId && !osctx->isBroadcasting()
          && (!enabled || (port > 0 && port < 65536));
        if (ok) {
          audio->setManagedEnabled(enabled, osctx->directHost(), port);
          osctx->pacer.setActive(enabled, std::chrono::steady_clock::now());
        }
        osctx->enqueueBundler(
          new AudioAckBundler("/set/audio/enabled/ack", moduleId, enabled, ok)
        );
        if (ok) ctrl->announceAudioStream();
      }, "audio");
    }
  );

  routes.emplace(
    "/set/audio/preset",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();
      int32_t preset = (args++)->AsInt32();

      ctrl->enqueueAction([=, this]() {
        bCast* audio = ctrl->managedAudio();
        bool ok = audio && audio->id == moduleId && !osctx->isBroadcasting()
          && bcast::isValidPreset(preset);
        if (ok) audio->setManagedPreset(preset);
        osctx->enqueueBundler(
          new AudioAckBundler("/set/audio/preset/ack", moduleId, preset, ok)
        );
        if (ok) ctrl->announceAudioStream();
      }, "audio");
    }
  );

  routes.emplace(
    "/audio/stats",
    [&](osc::ReceivedMessage::const_iterator& args, const IpEndpointName&) {
      int64_t moduleId = (args++)->AsInt64();
      ChunkPacer::AudioStats stats;
      stats.underruns = (args++)->AsInt32();
      stats.lost = (args++)->AsInt32();
      stats.recovered = (args++)->AsInt32();
      stats.jitterMs = (args++)->AsFloat();
      stats.bufferMs = (args++)->AsFloat();

      ctrl->enqueueAction([=, this]() {
        bCast* audio = ctrl->managedAudio();
        if (!audio || audio->id != moduleId || osctx->isBroadcasting()) return;

        bCastClientStats clientStats;
        clientStats.underruns = stats.underruns;
        clientStats.lost = stats.lost;
        clientStats.recovered = stats.recovered;
        clientStats.jitterMs = stats.jitterMs;
        clientStats.bufferMs = stats.bufferMs;
        audio->setClientStats(clientStats);

        auto now = std::chrono::steady_clock::now();
        osctx->pacer.onStats(stats, now);
        BENCH(
          bench::gauge("audio.client.underruns", stats.underruns);
          bench::gauge("audio.client.lost", stats.lost);
          bench::sample("audio.client.jitter_ms", stats.jitterMs);
          bench::gauge("tx.pacing.rate_mbps", (int64_t)osctx->pacer.rateMbps(now));
        )
      }, "audio");
    }
  );
```

`tx.pacing.rate_mbps` is set with each stats report, so a run that ends
with stats gone stale reports the last limit instead of 0. That's
acceptable for the bench, because stats only stop when the client does.

- [ ] **Step 5: Document the protocol in `API.md`**

Add a section before `## Typical Usage Flow`:

````markdown
### Audio (bCast expander)

A bCast module placed directly left or right of OSCctrl is managed by it.
Its own host/port/preset settings are ignored while managed, and it only
streams when the registered client asks. Audio goes over its own UDP
socket, not OSC (see "Audio packet format").

#### `/set/audio_stream <moduleId> <streamId> <presetId> <enabled> <epoch>`
**Direction:** Server → Client
**Purpose:** Describe the managed bCast's stream
**Arguments:**
  - `int64` moduleId
  - `int32` streamId (read as uint32; matches the packet header)
  - `int32` presetId (0 Lowest latency, 1 Balanced, 2 Robust, 3 Uncompressed)
  - `int32` enabled (0/1)
  - `int32` epoch (may lag the packets by one; packets are authoritative)

**Sent:** on registration, when a bCast attaches or detaches (detach sends
enabled 0), and after each successful `/set/audio/*` request.

#### `/set/audio/enabled <moduleId> <enabled> <port>`
**Direction:** Client → Server
**Purpose:** Start or stop the stream to this client's address
**Arguments:**
  - `int64` moduleId
  - `int32` enabled (0/1)
  - `int32` port (UDP port the client listens on, normally 7747)

**Response:** `/set/audio/enabled/ack <moduleId> <enabled> <success>`
(`int64`, `int32`, `int32`). Fails if the module isn't the managed bCast or
no client is registered.

**Usage:**
- Registering again, or missing heartbeats, stops the stream. Re-enable it
  after `/register`.
- While enabled, OSCctrl paces texture chunks from `/audio/stats`.

#### `/set/audio/preset <moduleId> <presetId>`
**Direction:** Client → Server
**Purpose:** Choose the latency/quality preset
**Arguments:**
  - `int64` moduleId
  - `int32` presetId (0–3)

**Response:** `/set/audio/preset/ack <moduleId> <presetId> <success>`

#### `/audio/stats <moduleId> <underruns> <lost> <recovered> <jitterMs> <bufferMs>`
**Direction:** Client → Server
**Purpose:** Jitter buffer health, every 500 ms while playing
**Arguments:**
  - `int64` moduleId
  - `int32` underruns, lost, recovered (cumulative since the client started
    this stream)
  - `float` jitterMs (95th percentile arrival jitter), bufferMs (current
    buffer level)

**Response:** None. New underruns or losses, or a jitter spike, slow
texture chunk sends. Clean reports speed them up again. Without stats for
2 s, pacing stops.

#### Audio packet format
UDP to the client's port, one datagram (≤ 1232 bytes) per frame. All
fields are little-endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | magic `RA` |
| 2 | 1 | version (1) |
| 3 | 1 | flags (0) |
| 4 | 4 | streamId (u32) |
| 8 | 2 | epoch (u16); a new epoch means flush and reset the decoder |
| 10 | 1 | codec (0 PCM s16le, 1 Opus) |
| 11 | 1 | channels (2) |
| 12 | 2 | frameSamples per channel at 48 kHz |
| 14 | 1 | frameCount |
| 15 | 1 | presetId |
| 16 | 4 | seq of the newest frame |
| 20 | … | frameCount × (u16 length, bytes), newest first; frame i has seq − i |

Older frames repeat earlier packets (redundancy) so a single lost packet
can be recovered.
````

- [ ] **Step 6: Build both flavours**

Run:

```bash
./build-wsl.sh -j8 BENCH=1 2>&1 | tail -5 && nm -C plugin.so | grep -c bench::
./build-wsl.sh -j8 2>&1 | tail -5 && nm -C plugin.so | grep -c bench::
tests/run.sh
```

Expected:
- both builds succeed;
- `bench::` count is nonzero for the first build and `0` for the second;
- `17 tests, 0 failed checks`.

- [ ] **Step 7: Live check, managed (the user runs it on Windows, BENCH build)**

The user builds with `./build-msys.sh -j8 BENCH=1` and places a bCast
directly right of OSCctrl, with a VCO into it. Then, from a native Windows
shell in `tools/bench`:
1. `python audio_bench.py run --duration 10 --preset 3 --analyze --json m3.json`.
   Expect:
   - the client discovers Rack and finds the stream;
   - the light turns blue at 30% and then full blue while streaming;
   - 0 lost and 0 glitches;
   - the bCast menu shows "Managed by OSCctrl" and the client stats;
   - at the end the stream is disabled (dim blue).
2. Repeat for presets 0, 1 and 2. Expect 0 lost with clean sine analysis.
3. `python audio_bench.py run --duration 20 --overlays 8 --json pace-on.json`
   and `... --no-chunk-pacing --json pace-off.json`, then
   `python audio_bench.py compare pace-off.json pace-on.json`. On wired
   localhost, pacing rarely triggers, so expect `server.pacing_rate_mbps`
   0 in both and overlay fps within noise. Wi-Fi A/B comes in Task 14.
4. Drag bCast away from OSCctrl mid-run. The client gets
   `/set/audio_stream ... enabled 0`, and the light goes to standalone
   (off, or green if Stream was on in its own menu).
5. Stop the client without cleanup (Ctrl+C twice). After the heartbeat
   timeout, `log.txt` shows `client is gone`, and the light goes dim blue.

- [ ] **Step 8: Stop.** The user reviews and commits.

---
## Task 10: UE dependencies and NetAudio protocol

All UE paths are relative to `/home/ont/dev/RackSim/Source/RackSim/`. UE
can't be built from WSL. Each UE task ends with the user building RackSim
and running the automation tests on Windows:

```bat
"C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" ^
  <RackSim dir>\RackSim.uproject ^
  -ExecCmds="Automation RunTests RackSim.NetAudio; Quit" ^
  -unattended -nullrhi -nosplash -nosound -log
```

They can also use the Session Frontend's Automation tab and filter on
`RackSim.NetAudio`.

**Files:**
- Modify: `RackSim.Build.cs`
- Create: `Comms/Audio/RSNetAudioProtocol.h`, `Comms/Audio/RSNetAudioProtocol.cpp`
- Test: `Tests/RSNetAudioProtocolTest.cpp`

**Interfaces:**
- Produces:
  - `RSNetAudio::` constants `SampleRate`, `Channels`, `HeaderSize`,
    `MaxDatagram`, `DefaultPort`, `Version`, `DefaultPresetId`;
  - `RSNetAudio::ECodec { PCM = 0, Opus = 1 }`;
  - `RSNetAudio::FPreset {Id, Codec, FrameSamples, Redundancy, TargetMs, MinMs, MaxMs}`
    and `const FPreset* FindPreset(int32)`;
  - `FRSAudioHeader {StreamId, Epoch, Codec, FrameSamples, PresetId, Seq}`;
  - `FRSAudioPacket {Header, Frames (newest first; Frames[i] has seq Header.Seq − i), ArrivalSec}`;
  - `bool RSNetAudio::ParsePacket(const uint8*, int32, FRSAudioPacket&)`;
  - `TArray<uint8> RSNetAudio::BuildPacket(const FRSAudioHeader&, const TArray<TArray<uint8>>&)`.
    Only the tests use it.

The UE side only decodes. On Win64 the bundled libOpus is 1.3.1; on Android
(Quest) UE 5.6 still links **1.1** (`libOpus.Build.cs` picks `opus-1.1` for
Android). Both decode CELT low-delay frames. The plugin encodes with 1.5.2,
and Opus streams are backward compatible.

- [ ] **Step 1: Add the modules**

In `RackSim.Build.cs`, add to `PublicDependencyModuleNames` after
`"RenderCore"`:

```csharp
      "RenderCore",
      "AudioMixer",
      "Sockets",
      "Networking"
```

After the `PrivateDependencyModuleNames.AddRange(...)` line, add:

```csharp
    // the engine's bundled libOpus, for NetAudio decoding
    AddEngineThirdPartyPrivateStaticDependencies(Target, "libOpus");
```

- [ ] **Step 2: Write the failing test**

`Tests/RSNetAudioProtocolTest.cpp`:

```cpp
#include "Comms/Audio/RSNetAudioProtocol.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FRSNetAudioProtocolTest,
  "RackSim.NetAudio.Protocol",
  EAutomationTestFlags_ApplicationContextMask |
    EAutomationTestFlags::ProductFilter
)

bool FRSNetAudioProtocolTest::RunTest(const FString& Parameters) {
  FRSAudioHeader Header;
  Header.StreamId = 0x04030201u;
  Header.Epoch = 0x0605;
  Header.Codec = RSNetAudio::ECodec::Opus;
  Header.FrameSamples = 480;
  Header.PresetId = 1;
  Header.Seq = 0x0A090807u;

  TArray<TArray<uint8>> Frames;
  Frames.Add(TArray<uint8>{0xAA, 0xBB, 0xCC});
  Frames.Add(TArray<uint8>{0x11});

  // the same bytes as gtnosft-rack's tests/test_packet.cpp
  TArray<uint8> Bytes = RSNetAudio::BuildPacket(Header, Frames);
  const uint8 Expected[] = {
    'R', 'A', 1, 0,
    0x01, 0x02, 0x03, 0x04,
    0x05, 0x06,
    1, 2,
    0xE0, 0x01,
    2, 1,
    0x07, 0x08, 0x09, 0x0A,
    3, 0, 0xAA, 0xBB, 0xCC,
    1, 0, 0x11,
  };
  TestEqual(TEXT("packet size"), Bytes.Num(), (int32)sizeof(Expected));
  for (int32 i = 0; i < Bytes.Num() && i < (int32)sizeof(Expected); ++i) {
    TestEqual(FString::Printf(TEXT("byte %d"), i), (int32)Bytes[i], (int32)Expected[i]);
  }

  FRSAudioPacket Packet;
  TestTrue(TEXT("parses"), RSNetAudio::ParsePacket(Bytes.GetData(), Bytes.Num(), Packet));
  TestEqual(TEXT("stream id"), (int64)Packet.Header.StreamId, (int64)0x04030201);
  TestEqual(TEXT("epoch"), (int32)Packet.Header.Epoch, 0x0605);
  TestTrue(TEXT("codec"), Packet.Header.Codec == RSNetAudio::ECodec::Opus);
  TestEqual(TEXT("frame samples"), (int32)Packet.Header.FrameSamples, 480);
  TestEqual(TEXT("preset"), (int32)Packet.Header.PresetId, 1);
  TestEqual(TEXT("seq"), (int64)Packet.Header.Seq, (int64)0x0A090807);
  TestEqual(TEXT("frame count"), Packet.Frames.Num(), 2);
  if (Packet.Frames.Num() == 2) {
    TestTrue(TEXT("newest frame"), Packet.Frames[0] == Frames[0]);
    TestTrue(TEXT("older frame"), Packet.Frames[1] == Frames[1]);
  }

  auto Rejects = [&](const TCHAR* What, TFunction<void(TArray<uint8>&)> Break) {
    TArray<uint8> Bad = Bytes;
    Break(Bad);
    FRSAudioPacket Out;
    TestFalse(What, RSNetAudio::ParsePacket(Bad.GetData(), Bad.Num(), Out));
  };
  Rejects(TEXT("bad magic"), [](TArray<uint8>& B) { B[0] = 'X'; });
  Rejects(TEXT("bad version"), [](TArray<uint8>& B) { B[2] = 2; });
  Rejects(TEXT("bad codec"), [](TArray<uint8>& B) { B[10] = 7; });
  Rejects(TEXT("mono"), [](TArray<uint8>& B) { B[11] = 1; });
  Rejects(TEXT("odd frame size"), [](TArray<uint8>& B) { B[12] = 100; B[13] = 0; });
  Rejects(TEXT("no frames"), [](TArray<uint8>& B) { B[14] = 0; });
  Rejects(TEXT("truncated frame"), [](TArray<uint8>& B) { B.Pop(); });
  Rejects(TEXT("short header"), [](TArray<uint8>& B) { B.SetNum(19); });

  // mirrors bcast::PRESETS in gtnosft-rack src/audio/Presets.hpp
  const RSNetAudio::FPreset* Balanced = RSNetAudio::FindPreset(1);
  TestTrue(TEXT("balanced exists"), Balanced != nullptr);
  if (Balanced) {
    TestEqual(TEXT("balanced frame"), Balanced->FrameSamples, 480);
    TestEqual(TEXT("balanced redundancy"), Balanced->Redundancy, 1);
    TestEqual(TEXT("balanced target"), Balanced->TargetMs, 40.f);
  }
  const RSNetAudio::FPreset* Pcm = RSNetAudio::FindPreset(3);
  TestTrue(TEXT("uncompressed is PCM"), Pcm && Pcm->Codec == RSNetAudio::ECodec::PCM);
  TestTrue(TEXT("unknown preset"), RSNetAudio::FindPreset(4) == nullptr);

  return true;
}
```

- [ ] **Step 3: Build and confirm the test fails**

The user builds RackSim. Expected: a compile error,
`Comms/Audio/RSNetAudioProtocol.h` not found.

- [ ] **Step 4: Write `Comms/Audio/RSNetAudioProtocol.h`**

```cpp
#pragma once

#include "CoreMinimal.h"

// bCast's UDP audio format. See gtnosft-rack API.md, "Audio packet format".
namespace RSNetAudio {
  constexpr int32 SampleRate = 48000;
  constexpr int32 Channels = 2;
  constexpr int32 HeaderSize = 20;
  constexpr int32 MaxDatagram = 1232;
  constexpr int32 DefaultPort = 7747;
  constexpr uint8 Version = 1;
  constexpr int32 DefaultPresetId = 1;

  enum class ECodec : uint8 { PCM = 0, Opus = 1 };

  struct FPreset {
    int32 Id;
    ECodec Codec;
    int32 FrameSamples;  // per channel, at 48 kHz
    int32 Redundancy;
    float TargetMs;
    float MinMs;
    float MaxMs;
  };

  // mirrors bcast::PRESETS in gtnosft-rack src/audio/Presets.hpp
  const FPreset* FindPreset(int32 Id);
}

struct FRSAudioHeader {
  uint32 StreamId = 0;
  uint16 Epoch = 0;
  RSNetAudio::ECodec Codec = RSNetAudio::ECodec::Opus;
  uint16 FrameSamples = 0;
  uint8 PresetId = 0;
  uint32 Seq = 0;  // of the newest frame
};

struct FRSAudioPacket {
  FRSAudioHeader Header;
  // newest first: Frames[i] has seq Header.Seq - i
  TArray<TArray<uint8>> Frames;
  double ArrivalSec = 0.0;
};

namespace RSNetAudio {
  bool ParsePacket(const uint8* Data, int32 Size, FRSAudioPacket& Out);
  TArray<uint8> BuildPacket(
    const FRSAudioHeader& Header,
    const TArray<TArray<uint8>>& Frames
  );
}
```

- [ ] **Step 5: Write `Comms/Audio/RSNetAudioProtocol.cpp`**

```cpp
#include "Comms/Audio/RSNetAudioProtocol.h"

namespace RSNetAudio {

static const FPreset Presets[] = {
  {0, ECodec::Opus, 240, 0, 20.f, 10.f, 60.f},
  {1, ECodec::Opus, 480, 1, 40.f, 20.f, 120.f},
  {2, ECodec::Opus, 960, 2, 80.f, 40.f, 200.f},
  {3, ECodec::PCM, 240, 0, 20.f, 10.f, 60.f},
};

const FPreset* FindPreset(int32 Id) {
  for (const FPreset& Preset : Presets) {
    if (Preset.Id == Id) return &Preset;
  }
  return nullptr;
}

static uint16 Get16(const uint8* P) {
  return (uint16)(P[0] | (P[1] << 8));
}

static uint32 Get32(const uint8* P) {
  return (uint32)P[0]
    | ((uint32)P[1] << 8)
    | ((uint32)P[2] << 16)
    | ((uint32)P[3] << 24);
}

static void Put16(TArray<uint8>& Out, uint16 V) {
  Out.Add(V & 0xff);
  Out.Add(V >> 8);
}

static void Put32(TArray<uint8>& Out, uint32 V) {
  for (int32 i = 0; i < 4; ++i) Out.Add((V >> (8 * i)) & 0xff);
}

bool ParsePacket(const uint8* Data, int32 Size, FRSAudioPacket& Out) {
  if (!Data || Size < HeaderSize) return false;
  if (Data[0] != 'R' || Data[1] != 'A' || Data[2] != Version) return false;
  if (Data[10] > (uint8)ECodec::Opus || Data[11] != Channels) return false;

  uint16 FrameSamples = Get16(Data + 12);
  if (FrameSamples != 240 && FrameSamples != 480 && FrameSamples != 960) return false;
  int32 FrameCount = Data[14];
  if (FrameCount < 1) return false;

  Out.Header.StreamId = Get32(Data + 4);
  Out.Header.Epoch = Get16(Data + 8);
  Out.Header.Codec = (ECodec)Data[10];
  Out.Header.FrameSamples = FrameSamples;
  Out.Header.PresetId = Data[15];
  Out.Header.Seq = Get32(Data + 16);

  Out.Frames.Reset(FrameCount);
  int32 Pos = HeaderSize;
  for (int32 i = 0; i < FrameCount; ++i) {
    if (Pos + 2 > Size) return false;
    int32 Length = Get16(Data + Pos);
    Pos += 2;
    if (Length < 1 || Pos + Length > Size) return false;
    Out.Frames.Emplace(Data + Pos, Length);
    Pos += Length;
  }
  return true;
}

TArray<uint8> BuildPacket(
  const FRSAudioHeader& Header,
  const TArray<TArray<uint8>>& Frames
) {
  TArray<uint8> Out;
  Out.Add('R');
  Out.Add('A');
  Out.Add(Version);
  Out.Add(0);
  Put32(Out, Header.StreamId);
  Put16(Out, Header.Epoch);
  Out.Add((uint8)Header.Codec);
  Out.Add(Channels);
  Put16(Out, Header.FrameSamples);
  Out.Add((uint8)Frames.Num());
  Out.Add(Header.PresetId);
  Put32(Out, Header.Seq);
  for (const TArray<uint8>& Frame : Frames) {
    Put16(Out, (uint16)Frame.Num());
    Out.Append(Frame);
  }
  return Out;
}

}
```

- [ ] **Step 6: Build and run the tests**

The user builds RackSim and runs `RackSim.NetAudio`.
Expected: `RackSim.NetAudio.Protocol` passes. The build links libOpus even
though nothing calls it yet.

If `AddEngineThirdPartyPrivateStaticDependencies` fails for Android,
`PrivateDependencyModuleNames.Add("libOpus")` is the equivalent.

- [ ] **Step 7: Stop.** The user reviews and commits (in RackSim).

---

## Task 11: FRSJitterBuffer

**Files:**
- Create: `Comms/Audio/RSJitterBuffer.h`, `Comms/Audio/RSJitterBuffer.cpp`
- Test: `Tests/RSJitterBufferTest.cpp`

**Interfaces:**
- Consumes: `FRSAudioPacket`, `RSNetAudio::FindPreset` and the constants
  (Task 10), and libOpus (`opus_decoder_create`, `opus_decode_float`).
- Produces `FRSJitterBuffer`:
  - `FStats {Underruns, Lost, Recovered, JitterMs, BufferMs}`;
  - `Push(const FRSAudioPacket&)`, `Pull(float* Out, int32 NumFrames, int32 OutRate)`
    (interleaved stereo), `Reset()`: audio render thread only;
  - `GetStats()`: any thread, reads atomics;
  - for tests: `GetTargetMs()`, `IsPlaying()`, `GetRatio()`.

Behaviour (the spec's pipeline, made concrete):
1. **Epochs:** a packet with a newer epoch flushes everything and restarts:
   new decoder, rebuffer. "Newer" is in u16 serial arithmetic. Older epochs
   (stragglers) are dropped. The codec and frame size are fixed per epoch.
2. **Reorder:** a map from seq to entry. Frames in a packet expand to
   (seq − i, data). The primary copy (i = 0) marks the entry. Frames older
   than the next due seq are dropped. A frame first played from a redundant
   copy counts as **recovered**.
3. **Decode when due:**
   - If the frame is present, decode it.
   - If it's missing and later frames exist, conceal it and count it as
     **lost**. Opus uses PLC for up to 3 frames, then silence. PCM repeats
     the last frame, fading out over 3 frames.
   - If nothing later exists, it's an **underrun**: fade the last 5 ms out,
     rebuffer, and boost the target by one frame for 10 s.
4. **Jitter:** for each packet, delay = arrival − seq × frame time. Jitter
   is the 95th percentile of (delay − min delay) over the last 2 s. It is
   reported as `JitterMs`.
5. **Target:** `clamp(max(preset target, jitter + 1 frame) + boost, min, max)`.
   It never drops below the preset target (deviation 7).
6. **Level:** decoded-but-unplayed audio plus queued frames up to the newest
   seq, measured after each pull renders. Playback (re)starts once the
   level would still be at the target after the next block.
7. **Rate:** a PI controller on (smoothed level − target) sets the
   resampling ratio (Kp 3e-4 /ms, Ki 4.5e-5 /ms·s, about 0.7 damping),
   clamped to ±0.5%. A Catmull-Rom interpolator resamples 48 kHz to
   `OutRate`.
8. **Resync:** a packet more than 2 × max ms ahead of the next due frame
   (after an outage, or runaway drift) flushes the queue and rebuffers from
   that packet. So does one more than 2 × max ms *behind* while the buffer
   isn't playing: the sender restarted with the same epoch (a reloaded
   patch keeps the module id, and the epoch starts at 0 again). While
   playing, such a packet is a straggler and is dropped.

Stats are cumulative from `Reset()`. Epoch changes don't clear them.

- [ ] **Step 1: Write the failing tests**

`Tests/RSJitterBufferTest.cpp`:

```cpp
#include "Comms/Audio/RSJitterBuffer.h"
#include "Comms/Audio/RSNetAudioProtocol.h"

#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"

#include "opus.h"

namespace {

constexpr int32 TestFlags =
  EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

float Sine(int64 SampleIndex) {
  return 0.5f * FMath::Sin(2.0 * UE_DOUBLE_PI * 1000.0 * SampleIndex / 48000.0);
}

// Simulates a bCast stream through a network into a jitter buffer pulled
// by an audio device (48 kHz in 256-frame blocks unless set).
struct FSim {
  RSNetAudio::ECodec Codec = RSNetAudio::ECodec::PCM;
  int32 PresetId = 3;
  int32 FrameSamples = 240;
  int32 Redundancy = 0;
  double SenderRate = 1.0;     // > 1: the sender's clock runs fast
  double EpochSwitchSec = -1;  // packets sent from here on use epoch 2, seq from 0
  bool bSwitchKeepsEpoch = false;  // the sender restarted: seq from 0, same epoch
  TFunction<bool(int64 Index)> Drop = [](int64) { return false; };
  TFunction<double(int64 Index, double SendSec)> Arrive =
    [](int64, double SendSec) { return SendSec + 0.002; };

  int32 Block = 256;
  int32 OutRate = 48000;
  FRSJitterBuffer Buffer;
  TArray<float> Output;
  int64 Dropped = 0;
  float MaxBufferMs = 0;

  void Run(double Seconds) {
    struct FArrival { double At; FRSAudioPacket Packet; };
    TArray<FArrival> Arrivals;
    TArray<TArray<uint8>> Encoded;

    OpusEncoder* Encoder = nullptr;
    if (Codec == RSNetAudio::ECodec::Opus) {
      int Error = 0;
      Encoder = opus_encoder_create(48000, 2, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &Error);
    }

    const double FrameSec = FrameSamples / 48000.0;
    const int64 SwitchIndex = EpochSwitchSec < 0
      ? MAX_int64
      : (int64)FMath::CeilToDouble(EpochSwitchSec * SenderRate / FrameSec);

    for (int64 Index = 0; ; ++Index) {
      double SendSec = Index * FrameSec / SenderRate;
      if (SendSec >= Seconds) break;

      TArray<float> Pcm;
      Pcm.SetNumUninitialized(FrameSamples * 2);
      for (int32 n = 0; n < FrameSamples; ++n) {
        Pcm[2 * n] = Pcm[2 * n + 1] = Sine(Index * FrameSamples + n);
      }
      TArray<uint8> Frame;
      if (Encoder) {
        Frame.SetNumUninitialized(1500);
        int32 Bytes = opus_encode_float(Encoder, Pcm.GetData(), FrameSamples, Frame.GetData(), 1500);
        Frame.SetNum(FMath::Max(Bytes, 0));
      } else {
        Frame.SetNumUninitialized(FrameSamples * 4);
        for (int32 i = 0; i < FrameSamples * 2; ++i) {
          int16 S = (int16)FMath::RoundToInt(Pcm[i] * 32767.f);
          Frame[2 * i] = (uint8)(S & 0xff);
          Frame[2 * i + 1] = (uint8)((S >> 8) & 0xff);
        }
      }
      Encoded.Add(MoveTemp(Frame));

      if (Drop(Index)) {
        ++Dropped;
        continue;
      }

      bool bSecondEpoch = Index >= SwitchIndex;
      int64 First = bSecondEpoch ? SwitchIndex : 0;
      FRSAudioPacket Packet;
      Packet.Header.StreamId = 7;
      Packet.Header.Epoch = (bSecondEpoch && !bSwitchKeepsEpoch) ? 2 : 1;
      Packet.Header.Codec = Codec;
      Packet.Header.FrameSamples = (uint16)FrameSamples;
      Packet.Header.PresetId = (uint8)PresetId;
      Packet.Header.Seq = (uint32)(Index - First);
      for (int64 r = 0; r <= Redundancy && Index - r >= First; ++r) {
        Packet.Frames.Add(Encoded[Index - r]);
      }
      double At = Arrive(Index, SendSec);
      Packet.ArrivalSec = At;
      Arrivals.Add({At, MoveTemp(Packet)});
    }
    if (Encoder) opus_encoder_destroy(Encoder);

    Arrivals.StableSort([](const FArrival& A, const FArrival& B) { return A.At < B.At; });

    int32 Next = 0;
    int64 Blocks = (int64)(Seconds * OutRate / Block);
    Output.SetNumZeroed(Blocks * Block * 2);
    for (int64 b = 0; b < Blocks; ++b) {
      double Now = (double)b * Block / OutRate;
      while (Next < Arrivals.Num() && Arrivals[Next].At <= Now) {
        Buffer.Push(Arrivals[Next++].Packet);
      }
      Buffer.Pull(Output.GetData() + b * Block * 2, Block, OutRate);
      if (Buffer.IsPlaying()) {
        MaxBufferMs = FMath::Max(MaxBufferMs, Buffer.GetStats().BufferMs);
      }
    }
  }

  // largest sample-to-sample step from FromSec on; a 1 kHz sine at 0.5
  // steps at most 0.065
  float MaxStep(double FromSec) const {
    float Max = 0;
    int64 First = FMath::Max<int64>(1, (int64)(FromSec * OutRate));
    for (int64 n = First; n < Output.Num() / 2; ++n) {
      for (int32 ch = 0; ch < 2; ++ch) {
        Max = FMath::Max(Max, FMath::Abs(Output[2 * n + ch] - Output[2 * (n - 1) + ch]));
      }
    }
    return Max;
  }

  float Peak(double FromSec) const {
    float Max = 0;
    for (int64 i = (int64)(FromSec * OutRate) * 2; i < Output.Num(); ++i) {
      Max = FMath::Max(Max, FMath::Abs(Output[i]));
    }
    return Max;
  }
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferInOrderTest, "RackSim.NetAudio.JitterBuffer.InOrder", TestFlags)
bool FRSJitterBufferInOrderTest::RunTest(const FString& Parameters) {
  FSim Sim;
  Sim.Run(3.0);
  FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
  TestEqual(TEXT("underruns"), Stats.Underruns, 0);
  TestEqual(TEXT("lost"), Stats.Lost, 0);
  TestEqual(TEXT("recovered"), Stats.Recovered, 0);
  TestTrue(TEXT("playing"), Sim.Buffer.IsPlaying());
  TestTrue(TEXT("continuous"), Sim.MaxStep(0.2) < 0.1f);
  TestTrue(TEXT("audible"), Sim.Peak(0.2) > 0.45f);
  TestTrue(
    TEXT("level near target"),
    FMath::Abs(Stats.BufferMs - Sim.Buffer.GetTargetMs()) < 10.f
  );
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferReorderTest, "RackSim.NetAudio.JitterBuffer.Reorder", TestFlags)
bool FRSJitterBufferReorderTest::RunTest(const FString& Parameters) {
  FSim Sim;
  // every 10th packet lands after the next one
  Sim.Arrive = [](int64 Index, double SendSec) {
    return SendSec + (Index % 10 == 5 ? 0.009 : 0.002);
  };
  Sim.Run(3.0);
  FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
  TestEqual(TEXT("underruns"), Stats.Underruns, 0);
  TestEqual(TEXT("lost"), Stats.Lost, 0);
  TestTrue(TEXT("continuous"), Sim.MaxStep(0.2) < 0.1f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferLossTest, "RackSim.NetAudio.JitterBuffer.LossWithoutRedundancy", TestFlags)
bool FRSJitterBufferLossTest::RunTest(const FString& Parameters) {
  FSim Sim;
  Sim.Codec = RSNetAudio::ECodec::Opus;
  Sim.PresetId = 0;
  FRandomStream Random(42);
  Sim.Drop = [&Random](int64 Index) { return Index >= 10 && Random.FRand() < 0.05f; };
  Sim.Run(5.0);
  FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
  TestTrue(TEXT("some packets dropped"), Sim.Dropped > 20);
  TestEqual(TEXT("underruns"), Stats.Underruns, 0);
  TestEqual(TEXT("recovered"), Stats.Recovered, 0);
  // drops in the last ~60 ms aren't due yet
  TestTrue(TEXT("lost counts drops"), Stats.Lost <= Sim.Dropped && Stats.Lost >= Sim.Dropped - 3);
  TestTrue(TEXT("audible"), Sim.Peak(4.0) > 0.3f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferRedundancyTest, "RackSim.NetAudio.JitterBuffer.LossWithRedundancy", TestFlags)
bool FRSJitterBufferRedundancyTest::RunTest(const FString& Parameters) {
  FSim Sim;
  // Balanced's framing and redundancy, with PCM frames so the output is exact
  Sim.PresetId = 1;
  Sim.FrameSamples = 480;
  Sim.Redundancy = 1;
  Sim.Drop = [](int64 Index) { return Index % 20 == 10; };
  Sim.Run(3.0);
  FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
  TestEqual(TEXT("underruns"), Stats.Underruns, 0);
  TestEqual(TEXT("lost"), Stats.Lost, 0);
  // drops in the last 50 ms aren't due yet
  TestTrue(
    TEXT("recovered counts drops"),
    Stats.Recovered <= Sim.Dropped && Stats.Recovered >= Sim.Dropped - 1
  );
  TestTrue(TEXT("continuous"), Sim.MaxStep(0.2) < 0.1f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferEpochTest, "RackSim.NetAudio.JitterBuffer.EpochReset", TestFlags)
bool FRSJitterBufferEpochTest::RunTest(const FString& Parameters) {
  FSim Sim;
  Sim.EpochSwitchSec = 1.0;
  Sim.Run(2.0);
  FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
  TestEqual(TEXT("underruns"), Stats.Underruns, 0);
  TestEqual(TEXT("lost"), Stats.Lost, 0);
  TestTrue(TEXT("playing again"), Sim.Buffer.IsPlaying());
  TestTrue(TEXT("audible after the switch"), Sim.Peak(1.5) > 0.45f);
  TestTrue(TEXT("continuous after the switch"), Sim.MaxStep(1.5) < 0.1f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferSenderRestartTest, "RackSim.NetAudio.JitterBuffer.SenderRestart", TestFlags)
bool FRSJitterBufferSenderRestartTest::RunTest(const FString& Parameters) {
  FSim Sim;
  Sim.EpochSwitchSec = 3.0;
  Sim.bSwitchKeepsEpoch = true;
  Sim.Run(5.0);
  TestTrue(TEXT("audible after the restart"), Sim.Peak(4.0) > 0.45f);
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferDriftTest, "RackSim.NetAudio.JitterBuffer.Drift", TestFlags)
bool FRSJitterBufferDriftTest::RunTest(const FString& Parameters) {
  for (double Rate : {1.003, 0.997}) {
    FSim Sim;
    Sim.PresetId = 1;
    Sim.FrameSamples = 480;
    Sim.SenderRate = Rate;
    Sim.Run(60.0);
    FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
    FString Label = FString::Printf(TEXT("rate %.3f"), Rate);
    TestEqual(Label + TEXT(" underruns"), Stats.Underruns, 0);
    TestTrue(
      Label + TEXT(" level near target"),
      FMath::Abs(Stats.BufferMs - Sim.Buffer.GetTargetMs()) < 15.f
    );
    TestTrue(Label + TEXT(" bounded"), Sim.MaxBufferMs < 120.f);
    TestTrue(
      Label + TEXT(" ratio tracks drift"),
      FMath::Abs(Sim.Buffer.GetRatio() - Rate) < 0.001
    );
    TestTrue(Label + TEXT(" continuous"), Sim.MaxStep(50.0) < 0.1f);
  }
  return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferBurstTest, "RackSim.NetAudio.JitterBuffer.Burst", TestFlags)
bool FRSJitterBufferBurstTest::RunTest(const FString& Parameters) {
  FSim Sim;
  Sim.PresetId = 1;
  Sim.FrameSamples = 480;
  Sim.Redundancy = 1;
  // everything sent in a 30 ms window arrives at its end
  Sim.Arrive = [](int64, double SendSec) {
    double Arrival = SendSec + 0.002;
    return (SendSec >= 8.0 && SendSec < 8.03) ? 8.032 : Arrival;
  };
  Sim.Run(10.0);
  FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
  TestEqual(TEXT("underruns"), Stats.Underruns, 0);
  TestEqual(TEXT("lost"), Stats.Lost, 0);
  TestTrue(TEXT("continuous"), Sim.MaxStep(0.2) < 0.1f);
  return true;
}

// RackSim's devices pull 1024 frames per callback: 44.1 kHz on Quest,
// 48 kHz on Windows (Config/DefaultEngine.ini)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRSJitterBufferDeviceBlocksTest, "RackSim.NetAudio.JitterBuffer.DeviceBlocks", TestFlags)
bool FRSJitterBufferDeviceBlocksTest::RunTest(const FString& Parameters) {
  for (int32 OutRate : {44100, 48000}) {
    FSim Sim;
    Sim.PresetId = 1;
    Sim.FrameSamples = 480;
    Sim.Redundancy = 1;
    Sim.Block = 1024;
    Sim.OutRate = OutRate;
    Sim.SenderRate = 0.997;
    Sim.Arrive = [](int64, double SendSec) {
      double Arrival = SendSec + 0.002;
      return (SendSec >= 8.0 && SendSec < 8.03) ? 8.032 : Arrival;
    };
    Sim.Run(30.0);
    FRSJitterBuffer::FStats Stats = Sim.Buffer.GetStats();
    FString Label = FString::Printf(TEXT("%d Hz"), OutRate);
    TestEqual(Label + TEXT(" underruns"), Stats.Underruns, 0);
    TestEqual(Label + TEXT(" lost"), Stats.Lost, 0);
    TestTrue(
      Label + TEXT(" ratio tracks drift"),
      FMath::Abs(Sim.Buffer.GetRatio() - 0.997) < 0.001
    );
    TestTrue(Label + TEXT(" continuous"), Sim.MaxStep(0.5) < 0.1f);
  }
  return true;
}
```

- [ ] **Step 2: Build and confirm the tests fail**

The user builds. Expected: a compile error, `Comms/Audio/RSJitterBuffer.h`
not found.

- [ ] **Step 3: Write `Comms/Audio/RSJitterBuffer.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Comms/Audio/RSNetAudioProtocol.h"

#include <atomic>

struct OpusDecoder;

// Reorders, decodes and conceals NetAudio frames, then resamples them to the
// output rate while steering the buffer level toward an adaptive target.
// Push, Pull and Reset run on the audio render thread only; GetStats is
// safe from any thread.
class RACKSIM_API FRSJitterBuffer {
public:
  struct FStats {
    int32 Underruns = 0;
    int32 Lost = 0;
    int32 Recovered = 0;
    float JitterMs = 0.f;
    float BufferMs = 0.f;
  };

  FRSJitterBuffer();
  ~FRSJitterBuffer();
  FRSJitterBuffer(const FRSJitterBuffer&) = delete;
  FRSJitterBuffer& operator=(const FRSJitterBuffer&) = delete;

  void Push(const FRSAudioPacket& Packet);
  // writes NumFrames interleaved stereo frames at OutRate
  void Pull(float* Out, int32 NumFrames, int32 OutRate);
  void Reset();
  FStats GetStats() const;

  float GetTargetMs() const { return TargetMs; }
  bool IsPlaying() const { return State == EState::Playing; }
  double GetRatio() const { return Ratio; }

private:
  enum class EState : uint8 { Empty, Buffering, Playing };

  struct FEntry {
    TArray<uint8> Data;
    bool bPrimary = false;
  };

  struct FArrival {
    double Sec;
    double Delay;
  };

  static constexpr double JitterWindowSec = 2.0;
  static constexpr double BoostSec = 10.0;
  static constexpr double FadeSec = 0.005;
  static constexpr int32 MaxConcealed = 3;
  static constexpr double Kp = 3e-4;
  static constexpr double Ki = 4.5e-5;
  static constexpr double MaxRatioDeviation = 0.005;
  static constexpr double LevelSmoothing = 0.02;

  void StartEpoch(const FRSAudioHeader& Header);
  void Flush();
  void Restart(uint32 OldestSeq, uint32 NewestSeq);
  void RecordArrival(uint32 Seq, double ArrivalSec);
  void UpdateTarget();
  float FrameMs() const;
  float LevelMs() const;
  int32 FifoFrames() const { return Fifo.Num() / RSNetAudio::Channels; }
  bool DecodeNext();
  void DecodeFrame(const TArray<uint8>* Data);
  int32 Render(float* Out, int32 NumFrames, double Step);
  void Compact();

  EState State = EState::Empty;
  bool bHaveEpoch = false;
  uint16 Epoch = 0;
  RSNetAudio::ECodec Codec = RSNetAudio::ECodec::Opus;
  int32 FrameSamples = 480;
  RSNetAudio::FPreset Preset;
  OpusDecoder* Decoder = nullptr;

  TMap<uint32, FEntry> Pending;
  uint32 NextSeq = 0;
  uint32 HighestSeq = 0;
  uint32 FirstSeq = 0;
  int32 Concealed = 0;
  TArray<float> LastFrame;

  // interleaved 48 kHz; one frame of history stays before ReadPos
  TArray<float> Fifo;
  double ReadPos = 1.0;
  int32 FadeInLeft = 0;

  TArray<FArrival> Arrivals;
  TArray<double> Spread;
  double LastArrivalSec = 0.0;
  float JitterMs = 0.f;
  float BoostMs = 0.f;
  double BoostUntilSec = 0.0;
  float TargetMs = 40.f;

  double SmoothedLevelMs = 0.0;
  double Integral = 0.0;
  double Ratio = 1.0;

  std::atomic<int32> Underruns{0};
  std::atomic<int32> Lost{0};
  std::atomic<int32> Recovered{0};
  std::atomic<float> JitterMsStat{0.f};
  std::atomic<float> BufferMsStat{0.f};
};
```

- [ ] **Step 4: Write `Comms/Audio/RSJitterBuffer.cpp`**

```cpp
#include "Comms/Audio/RSJitterBuffer.h"

#include "opus.h"

using RSNetAudio::Channels;
using RSNetAudio::SampleRate;

static float CatmullRom(float P0, float P1, float P2, float P3, float T) {
  return 0.5f * (
    2.f * P1
      + (P2 - P0) * T
      + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T * T
      + (3.f * (P1 - P2) + P3 - P0) * T * T * T
  );
}

FRSJitterBuffer::FRSJitterBuffer() {
  Reset();
}

FRSJitterBuffer::~FRSJitterBuffer() {
  if (Decoder) opus_decoder_destroy(Decoder);
}

void FRSJitterBuffer::Reset() {
  if (Decoder) {
    opus_decoder_destroy(Decoder);
    Decoder = nullptr;
  }
  bHaveEpoch = false;
  Preset = *RSNetAudio::FindPreset(RSNetAudio::DefaultPresetId);
  Flush();
  BoostMs = 0.f;
  Integral = 0.0;
  Ratio = 1.0;
  JitterMs = 0.f;
  TargetMs = Preset.TargetMs;
  Underruns = 0;
  Lost = 0;
  Recovered = 0;
  JitterMsStat = 0.f;
  BufferMsStat = 0.f;
}

void FRSJitterBuffer::Flush() {
  State = EState::Empty;
  Pending.Reset();
  Fifo.Reset();
  Fifo.AddZeroed(Channels);
  ReadPos = 1.0;
  Concealed = 0;
  LastFrame.Reset();
  Arrivals.Reset();
  SmoothedLevelMs = 0.0;
}

FRSJitterBuffer::FStats FRSJitterBuffer::GetStats() const {
  FStats Stats;
  Stats.Underruns = Underruns.load();
  Stats.Lost = Lost.load();
  Stats.Recovered = Recovered.load();
  Stats.JitterMs = JitterMsStat.load();
  Stats.BufferMs = BufferMsStat.load();
  return Stats;
}

float FRSJitterBuffer::FrameMs() const {
  return FrameSamples * 1000.f / SampleRate;
}

void FRSJitterBuffer::StartEpoch(const FRSAudioHeader& Header) {
  Flush();
  bHaveEpoch = true;
  Epoch = Header.Epoch;
  Codec = Header.Codec;
  FrameSamples = Header.FrameSamples;
  const RSNetAudio::FPreset* Found = RSNetAudio::FindPreset(Header.PresetId);
  Preset = Found ? *Found : *RSNetAudio::FindPreset(RSNetAudio::DefaultPresetId);

  if (Decoder) {
    opus_decoder_destroy(Decoder);
    Decoder = nullptr;
  }
  if (Codec == RSNetAudio::ECodec::Opus) {
    int Error = 0;
    Decoder = opus_decoder_create(SampleRate, Channels, &Error);
    if (Error != OPUS_OK) Decoder = nullptr;
  }
  UpdateTarget();
}

void FRSJitterBuffer::Restart(uint32 OldestSeq, uint32 NewestSeq) {
  Pending.Reset();
  Arrivals.Reset();
  NextSeq = OldestSeq;
  HighestSeq = NewestSeq;
  FirstSeq = NewestSeq;
  Concealed = 0;
  State = EState::Buffering;
}

void FRSJitterBuffer::Push(const FRSAudioPacket& Packet) {
  const FRSAudioHeader& Header = Packet.Header;
  if (Packet.Frames.Num() == 0) return;

  if (!bHaveEpoch || Header.Epoch != Epoch) {
    // a straggler from an older epoch
    if (bHaveEpoch && (int16)(uint16)(Header.Epoch - Epoch) < 0) return;
    StartEpoch(Header);
  }
  if (Header.Codec != Codec || Header.FrameSamples != FrameSamples) return;

  LastArrivalSec = Packet.ArrivalSec;
  uint32 Oldest = Header.Seq - (uint32)(Packet.Frames.Num() - 1);
  int32 MaxAhead = FMath::CeilToInt(2.f * Preset.MaxMs / FrameMs());
  int32 Ahead = (int32)(Header.Seq - NextSeq);
  // far behind while starved: the sender restarted with the same epoch
  bool bSenderRestarted = State != EState::Playing && Ahead < -MaxAhead;
  if (State == EState::Empty || Ahead > MaxAhead || bSenderRestarted) {
    Restart(Oldest, Header.Seq);
  }

  RecordArrival(Header.Seq, Packet.ArrivalSec);
  if ((int32)(Header.Seq - HighestSeq) > 0) HighestSeq = Header.Seq;

  for (int32 i = 0; i < Packet.Frames.Num(); ++i) {
    uint32 Seq = Header.Seq - (uint32)i;
    if ((int32)(Seq - NextSeq) < 0) continue;  // already played or concealed
    FEntry* Entry = Pending.Find(Seq);
    if (!Entry) {
      Entry = &Pending.Add(Seq);
      Entry->Data = Packet.Frames[i];
    }
    if (i == 0) Entry->bPrimary = true;
  }
  UpdateTarget();
}

void FRSJitterBuffer::RecordArrival(uint32 Seq, double ArrivalSec) {
  double FrameSec = FrameSamples / (double)SampleRate;
  double Delay = ArrivalSec - (double)(int32)(Seq - FirstSeq) * FrameSec;
  Arrivals.Add({ArrivalSec, Delay});

  int32 Old = 0;
  while (Old < Arrivals.Num() && ArrivalSec - Arrivals[Old].Sec > JitterWindowSec) ++Old;
  if (Old > 0) Arrivals.RemoveAt(0, Old, EAllowShrinking::No);

  double MinDelay = Arrivals[0].Delay;
  for (const FArrival& Arrival : Arrivals) MinDelay = FMath::Min(MinDelay, Arrival.Delay);
  Spread.Reset();
  for (const FArrival& Arrival : Arrivals) Spread.Add(Arrival.Delay - MinDelay);
  Spread.Sort();
  int32 Index = FMath::Clamp(FMath::CeilToInt(0.95 * Spread.Num()) - 1, 0, Spread.Num() - 1);
  JitterMs = (float)(Spread[Index] * 1000.0);
  JitterMsStat = JitterMs;
}

void FRSJitterBuffer::UpdateTarget() {
  if (BoostMs > 0.f && LastArrivalSec > BoostUntilSec) BoostMs = 0.f;
  float Base = FMath::Max(Preset.TargetMs, JitterMs + FrameMs());
  TargetMs = FMath::Clamp(Base + BoostMs, Preset.MinMs, Preset.MaxMs);
}

float FRSJitterBuffer::LevelMs() const {
  double Frames = FMath::Max(0.0, FifoFrames() - ReadPos);
  int32 Queued = (int32)(HighestSeq - NextSeq) + 1;
  if (State != EState::Empty && Queued > 0) Frames += (double)Queued * FrameSamples;
  return (float)(Frames * 1000.0 / SampleRate);
}

bool FRSJitterBuffer::DecodeNext() {
  if (FEntry* Entry = Pending.Find(NextSeq)) {
    if (!Entry->bPrimary) ++Recovered;
    Concealed = 0;
    DecodeFrame(&Entry->Data);
    Pending.Remove(NextSeq);
  } else if ((int32)(HighestSeq - NextSeq) > 0) {
    ++Lost;
    ++Concealed;
    DecodeFrame(nullptr);
  } else {
    return false;
  }
  ++NextSeq;
  return true;
}

void FRSJitterBuffer::DecodeFrame(const TArray<uint8>* Data) {
  const int32 Samples = FrameSamples * Channels;
  const int32 Start = Fifo.Num();
  Fifo.AddUninitialized(Samples);
  float* Dst = Fifo.GetData() + Start;

  bool bDecoded = false;
  if (Data && Codec == RSNetAudio::ECodec::PCM && Data->Num() == Samples * 2) {
    const uint8* P = Data->GetData();
    for (int32 i = 0; i < Samples; ++i) {
      int16 S = (int16)(uint16)(P[2 * i] | (P[2 * i + 1] << 8));
      Dst[i] = S / 32768.f;
    }
    bDecoded = true;
  } else if (Data && Codec == RSNetAudio::ECodec::Opus && Decoder) {
    bDecoded = opus_decode_float(Decoder, Data->GetData(), Data->Num(), Dst, FrameSamples, 0)
      == FrameSamples;
  }
  if (bDecoded) {
    LastFrame = TArray<float>(Dst, Samples);
    return;
  }

  if (Codec == RSNetAudio::ECodec::Opus && Decoder && Concealed <= MaxConcealed) {
    if (opus_decode_float(Decoder, nullptr, 0, Dst, FrameSamples, 0) == FrameSamples) return;
  }

  // repeat the last good frame, fading to silence over MaxConcealed frames
  if (LastFrame.Num() != Samples) LastFrame.Init(0.f, Samples);
  for (int32 n = 0; n < FrameSamples; ++n) {
    float Progress = (FMath::Max(Concealed - 1, 0) + (float)n / FrameSamples) / MaxConcealed;
    float Gain = FMath::Clamp(1.f - Progress, 0.f, 1.f);
    for (int32 ch = 0; ch < Channels; ++ch) {
      Dst[n * Channels + ch] = LastFrame[n * Channels + ch] * Gain;
    }
  }
}

int32 FRSJitterBuffer::Render(float* Out, int32 NumFrames, double Step) {
  const int32 Available = FifoFrames();
  const float* F = Fifo.GetData();
  int32 n = 0;
  for (; n < NumFrames; ++n) {
    int32 i = (int32)ReadPos;
    if (i + 2 >= Available) break;
    float T = (float)(ReadPos - i);
    for (int32 ch = 0; ch < Channels; ++ch) {
      Out[n * Channels + ch] = CatmullRom(
        F[(i - 1) * Channels + ch],
        F[i * Channels + ch],
        F[(i + 1) * Channels + ch],
        F[(i + 2) * Channels + ch],
        T
      );
    }
    ReadPos += Step;
  }
  return n;
}

void FRSJitterBuffer::Compact() {
  int32 Drop = FMath::Min((int32)ReadPos - 1, FifoFrames() - 1);
  if (Drop > 0) {
    Fifo.RemoveAt(0, Drop * Channels, EAllowShrinking::No);
    ReadPos -= Drop;
  }
}

void FRSJitterBuffer::Pull(float* Out, int32 NumFrames, int32 OutRate) {
  if (NumFrames <= 0) return;
  FMemory::Memzero(Out, sizeof(float) * NumFrames * Channels);
  if (OutRate <= 0) return;

  const double BlockSec = (double)NumFrames / OutRate;
  const float BlockMs = (float)(BlockSec * 1000.0);
  UpdateTarget();

  if (State != EState::Playing) {
    float Level = LevelMs();
    BufferMsStat = Level;
    if (State != EState::Buffering || Level - BlockMs < TargetMs) return;
    State = EState::Playing;
    FadeInLeft = FMath::Max(1, (int32)(FadeSec * OutRate));
    SmoothedLevelMs = Level - BlockMs;
  }

  double Error = SmoothedLevelMs - TargetMs;
  Integral = FMath::Clamp(
    Integral + Error * BlockSec,
    -MaxRatioDeviation / Ki,
    MaxRatioDeviation / Ki
  );
  Ratio = 1.0 + FMath::Clamp(Kp * Error + Ki * Integral, -MaxRatioDeviation, MaxRatioDeviation);
  const double Step = (double)SampleRate / OutRate * Ratio;

  const int32 Needed = (int32)(ReadPos + (NumFrames - 1) * Step) + 3;
  bool bUnderrun = false;
  while (FifoFrames() < Needed) {
    if (!DecodeNext()) {
      bUnderrun = true;
      break;
    }
  }

  const int32 Rendered = Render(Out, NumFrames, Step);
  const int32 FadeLength = FMath::Max(1, (int32)(FadeSec * OutRate));
  for (int32 n = 0; n < Rendered && FadeInLeft > 0; ++n, --FadeInLeft) {
    float Gain = 1.f - (float)FadeInLeft / FadeLength;
    for (int32 ch = 0; ch < Channels; ++ch) Out[n * Channels + ch] *= Gain;
  }

  if (bUnderrun) {
    int32 Fade = FMath::Min(Rendered, FadeLength);
    for (int32 j = 0; j < Fade; ++j) {
      float Gain = 1.f - (float)(j + 1) / Fade;
      int32 n = Rendered - Fade + j;
      for (int32 ch = 0; ch < Channels; ++ch) Out[n * Channels + ch] *= Gain;
    }
    ++Underruns;
    State = EState::Buffering;
    BoostMs += FrameMs();
    BoostUntilSec = LastArrivalSec + BoostSec;
    UpdateTarget();
  }

  Compact();
  float Level = LevelMs();
  BufferMsStat = Level;
  SmoothedLevelMs += (Level - SmoothedLevelMs) * LevelSmoothing;
}
```

Notes for the implementer:
- `Level − BlockMs < TargetMs` makes playback start with one block of
  headroom. From then on the level is measured after the render, so the
  controller holds the post-render level at the target.
- `Ratio > 1` consumes input faster, which drains a level that is too high.
  The drift test checks that `Ratio` settles at the sender's rate.
- `LastFrame = TArray<float>(Dst, Samples)` copies before `Fifo` can
  reallocate.

- [ ] **Step 5: Build and run the tests**

The user builds RackSim and runs `RackSim.NetAudio`.
Expected: Protocol plus 9 JitterBuffer tests pass. The 60 s drift test
takes a few seconds.

If a test fails, debug it with the systematic-debugging skill before
changing any constant. Add `UE_LOG(LogTemp, Display, ...)` of the level,
target and ratio per second inside `FSim::Run`; it's the quickest way to
see what the controller is doing. Most likely to need tuning:
- **Burst:** the margin is about 5 ms. If it underruns, check that the
  level settled at about 40 ms before 8 s. Don't raise the Balanced target
  to make it pass.
- **Drift:** if `Ratio` oscillates, lower `Kp` and `Ki` together.
- **DeviceBlocks:** 1024-frame pulls move the level 21–23 ms per callback.
  If only this test fails, check the start condition (`Level − BlockMs`)
  and the level smoothing before touching the gains.

- [ ] **Step 6: Stop.** The user reviews and commits (in RackSim).

---

---

## Task 12: Receiver, synth component and speaker

**Files:**
- Create: `Comms/Audio/RSAudioReceiver.h`, `Comms/Audio/RSAudioReceiver.cpp`
- Create: `Comms/Audio/RSNetAudioComponent.h`, `Comms/Audio/RSNetAudioComponent.cpp`
- Create: `Comms/Audio/RSNetAudioSpeaker.h`, `Comms/Audio/RSNetAudioSpeaker.cpp`

**Interfaces:**
- Consumes: `RSNetAudio::ParsePacket`, `FRSAudioPacket` and the constants
  (Task 10); `FRSJitterBuffer` (Task 11); `LogRackSim` (`RSLogSubsystem.h`).
- Produces:
  - `FRSAudioReceiver : FRunnable`. Game thread: `bool Start(int32 Port)`,
    `Shutdown()`, `SetActiveStream(uint32)`, `ClearActiveStream()`,
    `GetCounters()`. Audio render thread, the only consumer:
    `bool Dequeue(FRSAudioPacket&)`, `Flush()`.
  - `URSNetAudioComponent : USynthComponent`. Game thread:
    `bool Listen(int32 Port = RSNetAudio::DefaultPort)`, `Play(uint32 StreamId)`,
    `StopPlayback()`, `GetStats()`, `GetCounters()`.
  - `ARSNetAudioSpeaker : AActor` with the component as root:
    `bool Play(uint32 StreamId)` (listens on first use), `StopPlayback()`,
    `IsPlaying()`, `GetStats()`.
  - Console commands for testing without OSCctrl: `rs.NetAudio.DebugPlay <streamId>`,
    `rs.NetAudio.DebugStats`, `rs.NetAudio.DebugStop`.

Threads:
- The receiver thread blocks in `Wait(WaitForRead, 50 ms)`, drains the
  socket, and filters on the active stream id (an atomic).
- `OnGenerateAudio` runs on the audio render thread. It is the only caller
  of `Dequeue`, `Push`, `Pull` and `Reset`, so `TSpscQueue`'s single
  consumer rule holds. `Play` asks for a reset through an atomic flag
  instead of touching the buffer from the game thread.
- `OnGenerateAudio` is the older `USynthComponent` path (UE's comment says
  `ISoundGenerator` is preferred, but it isn't deprecated in 5.6). A sound
  generator is recreated on every `Start()`, which would hand the queue to a
  second consumer. Keep this path unless it's deprecated.

`NumSamples` in `OnGenerateAudio` counts interleaved samples, so the frame
count is `NumSamples / 2`. The device rate comes from `Init` (48 kHz on
Windows, 44.1 kHz on Quest per `Config/DefaultEngine.ini`).

There is no automated test: this task is sockets, threads and the audio
device. The jitter buffer logic is already covered. The check is by ear,
with the mock server's 1 kHz sine.

- [ ] **Step 1: Write `Comms/Audio/RSAudioReceiver.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "Containers/SpscQueue.h"
#include "Comms/Audio/RSNetAudioProtocol.h"

#include <atomic>

class FSocket;
class FRunnableThread;

// Receives bCast packets on its own thread and queues them for the audio
// render thread. Never touches UObjects.
class RACKSIM_API FRSAudioReceiver : public FRunnable {
public:
  struct FCounters {
    int32 Received = 0;
    int32 Invalid = 0;
    int32 Foreign = 0;   // valid, but not the active stream
    int32 Overflow = 0;  // dropped because the queue was full
  };

  FRSAudioReceiver() = default;
  virtual ~FRSAudioReceiver() override;
  FRSAudioReceiver(const FRSAudioReceiver&) = delete;
  FRSAudioReceiver& operator=(const FRSAudioReceiver&) = delete;

  // game thread
  bool Start(int32 Port);
  void Shutdown();
  bool IsListening() const { return Thread != nullptr; }
  void SetActiveStream(uint32 StreamId) { ActiveStream = (int64)StreamId; }
  void ClearActiveStream() { ActiveStream = -1; }
  FCounters GetCounters() const;

  // audio render thread only
  bool Dequeue(FRSAudioPacket& Out);
  void Flush();

  // FRunnable
  virtual uint32 Run() override;
  virtual void Stop() override { bRunning = false; }

private:
  static constexpr int32 MaxQueued = 512;
  static constexpr int32 ReceiveBufferBytes = 1 << 20;

  FSocket* Socket = nullptr;
  FRunnableThread* Thread = nullptr;
  int32 BoundPort = 0;
  std::atomic<bool> bRunning{false};
  std::atomic<int64> ActiveStream{-1};

  TSpscQueue<FRSAudioPacket> Queue;
  std::atomic<int32> Queued{0};

  std::atomic<int32> Received{0};
  std::atomic<int32> Invalid{0};
  std::atomic<int32> Foreign{0};
  std::atomic<int32> Overflow{0};
};
```

- [ ] **Step 2: Write `Comms/Audio/RSAudioReceiver.cpp`**

```cpp
#include "Comms/Audio/RSAudioReceiver.h"

#include "RSLogSubsystem.h"

#include "Common/UdpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

FRSAudioReceiver::~FRSAudioReceiver() {
  Shutdown();
}

bool FRSAudioReceiver::Start(int32 Port) {
  if (Thread) return BoundPort == Port;

  Socket = FUdpSocketBuilder(TEXT("RSNetAudio"))
    .AsNonBlocking()
    .BoundToPort((uint16)Port)
    .WithReceiveBufferSize(ReceiveBufferBytes)
    .Build();
  if (!Socket) {
    UE_LOG(LogRackSim, Error, TEXT("NetAudio: can't bind UDP port %d"), Port);
    return false;
  }

  BoundPort = Port;
  bRunning = true;
  Thread = FRunnableThread::Create(this, TEXT("RSNetAudioRx"), 0, TPri_AboveNormal);
  if (!Thread) {
    UE_LOG(LogRackSim, Error, TEXT("NetAudio: can't start the receive thread"));
    bRunning = false;
    Shutdown();
    return false;
  }
  UE_LOG(LogRackSim, Log, TEXT("NetAudio: listening on UDP %d"), Port);
  return true;
}

void FRSAudioReceiver::Shutdown() {
  if (Thread) {
    Thread->Kill(true);  // calls Stop(), then waits for Run() to return
    delete Thread;
    Thread = nullptr;
  }
  if (Socket) {
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
    Socket = nullptr;
  }
  BoundPort = 0;
}

FRSAudioReceiver::FCounters FRSAudioReceiver::GetCounters() const {
  FCounters Counters;
  Counters.Received = Received.load();
  Counters.Invalid = Invalid.load();
  Counters.Foreign = Foreign.load();
  Counters.Overflow = Overflow.load();
  return Counters;
}

bool FRSAudioReceiver::Dequeue(FRSAudioPacket& Out) {
  if (!Queue.Dequeue(Out)) return false;
  --Queued;
  return true;
}

void FRSAudioReceiver::Flush() {
  FRSAudioPacket Discard;
  while (Dequeue(Discard)) {}
}

uint32 FRSAudioReceiver::Run() {
  TArray<uint8> Buffer;
  Buffer.SetNumUninitialized(RSNetAudio::MaxDatagram + 1);
  TSharedRef<FInternetAddr> From =
    ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();

  while (bRunning) {
    if (!Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromMilliseconds(50))) {
      continue;
    }

    int32 Bytes = 0;
    while (bRunning && Socket->RecvFrom(Buffer.GetData(), Buffer.Num(), Bytes, *From)) {
      const double ArrivalSec = FPlatformTime::Seconds();

      FRSAudioPacket Packet;
      if (!RSNetAudio::ParsePacket(Buffer.GetData(), Bytes, Packet)) {
        ++Invalid;
        continue;
      }
      const int64 Active = ActiveStream.load();
      if (Active < 0 || Packet.Header.StreamId != (uint32)Active) {
        ++Foreign;
        continue;
      }
      if (Queued.load() >= MaxQueued) {
        ++Overflow;
        continue;
      }
      Packet.ArrivalSec = ArrivalSec;
      Queue.Enqueue(MoveTemp(Packet));
      ++Queued;
      ++Received;
    }
  }
  return 0;
}
```

`MaxQueued` is 512 packets: 2.5 s at 5 ms frames. The queue only fills
when the synth isn't pulling (stopped, or the app is suspended); the next
`Play` flushes it.

- [ ] **Step 3: Write `Comms/Audio/RSNetAudioComponent.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Comms/Audio/RSAudioReceiver.h"
#include "Comms/Audio/RSJitterBuffer.h"

#include <atomic>

#include "RSNetAudioComponent.generated.h"

// Plays one bCast stream: stereo, not positioned in the world.
UCLASS()
class RACKSIM_API URSNetAudioComponent : public USynthComponent {
  GENERATED_BODY()

public:
  URSNetAudioComponent(const FObjectInitializer& ObjectInitializer);

  // game thread
  bool Listen(int32 Port = RSNetAudio::DefaultPort);
  // starts playing StreamId from a fresh jitter buffer
  void Play(uint32 StreamId);
  void StopPlayback();
  FRSJitterBuffer::FStats GetStats() const { return Buffer.GetStats(); }
  FRSAudioReceiver::FCounters GetCounters() const { return Receiver.GetCounters(); }

  virtual void EndPlay(const EEndPlayReason::Type Reason) override;
  virtual void BeginDestroy() override;

protected:
  virtual bool Init(int32& SampleRate) override;
  virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
  FRSAudioReceiver Receiver;
  FRSJitterBuffer Buffer;
  std::atomic<bool> bResetRequested{true};
  std::atomic<int32> OutputRate{RSNetAudio::SampleRate};
};
```

- [ ] **Step 4: Write `Comms/Audio/RSNetAudioComponent.cpp`**

```cpp
#include "Comms/Audio/RSNetAudioComponent.h"

URSNetAudioComponent::URSNetAudioComponent(
  const FObjectInitializer& ObjectInitializer
) : Super(ObjectInitializer) {
  bAutoActivate = false;
  bAllowSpatialization = false;
  NumChannels = RSNetAudio::Channels;
}

bool URSNetAudioComponent::Listen(int32 Port) {
  return Receiver.Start(Port);
}

void URSNetAudioComponent::Play(uint32 StreamId) {
  Receiver.SetActiveStream(StreamId);
  bResetRequested = true;
  if (!IsPlaying()) Start();
}

void URSNetAudioComponent::StopPlayback() {
  Receiver.ClearActiveStream();
  Stop();
}

void URSNetAudioComponent::EndPlay(const EEndPlayReason::Type Reason) {
  Receiver.Shutdown();
  Super::EndPlay(Reason);
}

void URSNetAudioComponent::BeginDestroy() {
  Receiver.Shutdown();
  Super::BeginDestroy();
}

bool URSNetAudioComponent::Init(int32& SampleRate) {
  NumChannels = RSNetAudio::Channels;
  OutputRate = SampleRate;
  bResetRequested = true;
  return true;
}

int32 URSNetAudioComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples) {
  if (bResetRequested.exchange(false)) {
    Receiver.Flush();
    Buffer.Reset();
  }

  FRSAudioPacket Packet;
  while (Receiver.Dequeue(Packet)) Buffer.Push(Packet);

  Buffer.Pull(OutAudio, NumSamples / RSNetAudio::Channels, OutputRate.load());
  return NumSamples;
}
```

- [ ] **Step 5: Write `Comms/Audio/RSNetAudioSpeaker.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Comms/Audio/RSJitterBuffer.h"

#include "RSNetAudioSpeaker.generated.h"

class URSNetAudioComponent;

// Plays a bCast stream. Placing virtual speakers later means moving this
// actor and enabling attenuation on its component.
UCLASS()
class RACKSIM_API ARSNetAudioSpeaker : public AActor {
  GENERATED_BODY()

public:
  ARSNetAudioSpeaker();

  // listens on RSNetAudio::DefaultPort on first use
  bool Play(uint32 StreamId);
  void StopPlayback();
  bool IsPlaying() const;
  FRSJitterBuffer::FStats GetStats() const;

  URSNetAudioComponent* GetAudio() const { return Audio; }

private:
  UPROPERTY(VisibleAnywhere)
  TObjectPtr<URSNetAudioComponent> Audio;
};
```

- [ ] **Step 6: Write `Comms/Audio/RSNetAudioSpeaker.cpp`**

```cpp
#include "Comms/Audio/RSNetAudioSpeaker.h"

#include "Comms/Audio/RSNetAudioComponent.h"
#include "RSLogSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

ARSNetAudioSpeaker::ARSNetAudioSpeaker() {
  PrimaryActorTick.bCanEverTick = false;
  Audio = CreateDefaultSubobject<URSNetAudioComponent>(TEXT("NetAudio"));
  SetRootComponent(Audio);
}

bool ARSNetAudioSpeaker::Play(uint32 StreamId) {
  if (!Audio->Listen()) return false;
  Audio->Play(StreamId);
  return true;
}

void ARSNetAudioSpeaker::StopPlayback() {
  Audio->StopPlayback();
}

bool ARSNetAudioSpeaker::IsPlaying() const {
  return Audio->IsPlaying();
}

FRSJitterBuffer::FStats ARSNetAudioSpeaker::GetStats() const {
  return Audio->GetStats();
}

// Testing without OSCctrl, e.g. against `mock_server.py --audio-to`.
// RackSim itself only plays streams that OSCctrl offers.
namespace {
TWeakObjectPtr<ARSNetAudioSpeaker> GDebugSpeaker;

FAutoConsoleCommandWithWorldAndArgs GNetAudioDebugPlay(
  TEXT("rs.NetAudio.DebugPlay"),
  TEXT("rs.NetAudio.DebugPlay <streamId>: play a bCast stream without OSCctrl (testing)"),
  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
    [](const TArray<FString>& Args, UWorld* World) {
      if (!World || Args.Num() < 1) return;
      const uint32 StreamId = (uint32)FCString::Strtoui64(*Args[0], nullptr, 10);
      if (!GDebugSpeaker.IsValid()) {
        GDebugSpeaker = World->SpawnActor<ARSNetAudioSpeaker>();
      }
      if (!GDebugSpeaker.IsValid() || !GDebugSpeaker->Play(StreamId)) {
        UE_LOG(LogRackSim, Error, TEXT("NetAudio: debug play failed"));
        return;
      }
      UE_LOG(LogRackSim, Log, TEXT("NetAudio: debug playing stream %u"), StreamId);
    }
  )
);

FAutoConsoleCommandWithWorldAndArgs GNetAudioDebugStats(
  TEXT("rs.NetAudio.DebugStats"),
  TEXT("rs.NetAudio.DebugStats: log the debug speaker's jitter buffer and receiver stats"),
  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
    [](const TArray<FString>&, UWorld*) {
      if (!GDebugSpeaker.IsValid()) return;
      FRSJitterBuffer::FStats Stats = GDebugSpeaker->GetStats();
      FRSAudioReceiver::FCounters Counters = GDebugSpeaker->GetAudio()->GetCounters();
      UE_LOG(LogRackSim, Log,
        TEXT("NetAudio: underruns %d lost %d recovered %d jitter %.1f ms buffer %.1f ms | received %d invalid %d foreign %d overflow %d"),
        Stats.Underruns, Stats.Lost, Stats.Recovered, Stats.JitterMs, Stats.BufferMs,
        Counters.Received, Counters.Invalid, Counters.Foreign, Counters.Overflow);
    }
  )
);

FAutoConsoleCommandWithWorldAndArgs GNetAudioDebugStop(
  TEXT("rs.NetAudio.DebugStop"),
  TEXT("rs.NetAudio.DebugStop: destroy the debug speaker"),
  FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
    [](const TArray<FString>&, UWorld*) {
      if (GDebugSpeaker.IsValid()) GDebugSpeaker->Destroy();
      GDebugSpeaker.Reset();
    }
  )
);
}
```

- [ ] **Step 7: Build**

The user builds RackSim (Win64 editor) and runs `RackSim.NetAudio`.
Expected: it compiles, and the 10 NetAudio tests still pass.

If `USynthComponent` doesn't link, add `"AudioExtensions"` to
`PublicDependencyModuleNames` next to `"AudioMixer"`.

- [ ] **Step 8: Listen check with the mock sender (the user runs it on Windows)**

From a native Windows shell in `gtnosft-rack/tools/bench`:

```bat
python mock_server.py --port 7299 --audio-to 127.0.0.1:7747
```

Then start a PIE session in RackSim and, in its console (`~`):

```
rs.NetAudio.DebugPlay 4242
```

Expected:
- a clean, steady 1 kHz tone in both ears within about 100 ms;
- after a few seconds, `rs.NetAudio.DebugStats` shows 0 underruns, 0 lost,
  jitter under 2 ms, a buffer of about 20 ms (Uncompressed target), and
  `received` rising with `invalid`, `foreign` and `overflow` at 0;
- `rs.NetAudio.DebugPlay 1` (a stream that doesn't exist): silence, and
  `foreign` rises;
- `rs.NetAudio.DebugPlay 4242` again: the tone comes back;
- stopping the mock server: silence after about 20 ms and one underrun.
  Restarting it: the tone comes back on its own (seq restarts at 0 with the
  same epoch; see Task 11, resync);
- `rs.NetAudio.DebugStop`, then ending PIE: no warnings, and starting PIE
  again can bind port 7747.

Optionally repeat on Quest with `--audio-to <quest ip>:7747`, using a
development build's console.

- [ ] **Step 9: Stop.** The user reviews and commits (in RackSim).

---

## Task 13: CommunicationsManager integration

**Files:**
- Modify: `Comms/CommunicationsManager.h`, `Comms/CommunicationsManager.cpp`
- Modify: `Comms/OSCReceiver.h`, `Comms/OSCReceiver.cpp`
- Modify: `Comms/OSCSender.h`, `Comms/OSCSender.cpp`

**Interfaces:**
- Consumes: `ARSNetAudioSpeaker` (Task 12); `RSNetAudio::DefaultPort`,
  `DefaultPresetId` and `FindPreset` (Task 10); the Rack routes from Task 9:
  - Rack → client: `/set/audio_stream h i i i i`,
    `/set/audio/enabled/ack h i i`, `/set/audio/preset/ack h i i`;
  - client → Rack: `/set/audio/enabled h i i`, `/set/audio/preset h i`,
    `/audio/stats h i i i f f`.
- Produces:
  - `ACommunicationsManager::HandleAudioStream`, `AckAudioEnabled` and
    `AckAudioPreset`;
  - `UOSCSender::TxSetAudioEnabled`, `TxSetAudioPreset` and `TxAudioStats`;
  - the console variable `rs.NetAudio.Preset` (default 1).

Every flag and ack field is an OSC `int32`: Rack reads them with
`AsInt32` and sends acks the same way, so use `AddInt32` and `GetInt32`,
never `AddBool` or `GetBool`.

Flow:
1. On registration and on attach, Rack sends `/set/audio_stream ... enabled 0`
   (an offer).
2. RackSim stops any current playback, starts the speaker on that stream
   id, and sends `/set/audio/enabled <module> 1 7747` (state `Requested`).
3. Rack acks and then re-announces with `enabled 1` (state `Playing`).
4. While playing, a 500 ms timer sends `/audio/stats` and checks
   `rs.NetAudio.Preset`. If it differs from the announced preset (and wasn't
   just requested), the timer sends `/set/audio/preset`. Rack acks and
   re-announces with the new preset.
5. On detach, Rack sends `enabled 0` for the old module. RackSim treats it
   like any offer and asks again. The request fails, and the failed ack
   stops playback (deviation 8).
6. `Stop()` (which also runs on connection loss through `Quiesce`) sends
   `/set/audio/enabled <module> 0 0` if audio was active, then stops and
   destroys the speaker. A registration failure also destroys it.

The epoch in `/set/audio_stream` is ignored: packets are authoritative.

- [ ] **Step 1: Add the Tx calls to `UOSCSender`**

`Comms/OSCSender.h`, after `void TxRemoveCable(int64 CableId);`:

```cpp

  void TxSetAudioEnabled(int64 ModuleId, bool bEnabled, int32 Port);
  void TxSetAudioPreset(int64 ModuleId, int32 PresetId);
  void TxAudioStats(
    int64 ModuleId,
    int32 Underruns,
    int32 Lost,
    int32 Recovered,
    float JitterMs,
    float BufferMs
  );
```

`Comms/OSCSender.cpp`, at the end:

```cpp

void UOSCSender::TxSetAudioEnabled(int64 ModuleId, bool bEnabled, int32 Port) {
  FOSCAddress address =
    UOSCManager::ConvertStringToOSCAddress(FString("/set/audio/enabled"));
  FOSCMessage message;
  UOSCManager::SetOSCMessageAddress(message, address);

  UOSCManager::AddInt64(message, ModuleId);
  UOSCManager::AddInt32(message, bEnabled ? 1 : 0);
  UOSCManager::AddInt32(message, Port);

  Sender->SendOSCMessage(message);
}

void UOSCSender::TxSetAudioPreset(int64 ModuleId, int32 PresetId) {
  FOSCAddress address =
    UOSCManager::ConvertStringToOSCAddress(FString("/set/audio/preset"));
  FOSCMessage message;
  UOSCManager::SetOSCMessageAddress(message, address);

  UOSCManager::AddInt64(message, ModuleId);
  UOSCManager::AddInt32(message, PresetId);

  Sender->SendOSCMessage(message);
}

void UOSCSender::TxAudioStats(
  int64 ModuleId,
  int32 Underruns,
  int32 Lost,
  int32 Recovered,
  float JitterMs,
  float BufferMs
) {
  FOSCAddress address =
    UOSCManager::ConvertStringToOSCAddress(FString("/audio/stats"));
  FOSCMessage message;
  UOSCManager::SetOSCMessageAddress(message, address);

  UOSCManager::AddInt64(message, ModuleId);
  UOSCManager::AddInt32(message, Underruns);
  UOSCManager::AddInt32(message, Lost);
  UOSCManager::AddInt32(message, Recovered);
  UOSCManager::AddFloat(message, JitterMs);
  UOSCManager::AddFloat(message, BufferMs);

  Sender->SendOSCMessage(message);
}
```

- [ ] **Step 2: Add the routes to `UOSCReceiver`**

`Comms/OSCReceiver.h`: add to the `// state` group, after `RxLightState`:

```cpp
  void RxAudioStream(const FOSCMessage& Message, const FString& IPAddress);
```

And to the `// ack` group, after `RxAckLightSubscription`:

```cpp
  void RxAckAudioEnabled(const FOSCMessage& Message, const FString& IPAddress);
  void RxAckAudioPreset(const FOSCMessage& Message, const FString& IPAddress);
```

`Comms/OSCReceiver.cpp`: at the end of `GenerateRoutes()`, after the
`/ack/subscribe/module/lights` route:

```cpp

  Routes.Add(
    TEXT("/set/audio_stream"),
    [this](const FOSCMessage& Message, const FString& IPAddress) {
      RxAudioStream(Message, IPAddress);
    }
  );
  Routes.Add(
    TEXT("/set/audio/enabled/ack"),
    [this](const FOSCMessage& Message, const FString& IPAddress) {
      RxAckAudioEnabled(Message, IPAddress);
    }
  );
  Routes.Add(
    TEXT("/set/audio/preset/ack"),
    [this](const FOSCMessage& Message, const FString& IPAddress) {
      RxAckAudioPreset(Message, IPAddress);
    }
  );
```

At the end of the file:

```cpp

void UOSCReceiver::RxAudioStream(const FOSCMessage& Message, const FString& IPAddress) {
  int msgIdx = -1;

  int64 moduleId;
  UOSCManager::GetInt64(Message, ++msgIdx, moduleId);
  int32 streamId;
  UOSCManager::GetInt32(Message, ++msgIdx, streamId);
  int32 presetId;
  UOSCManager::GetInt32(Message, ++msgIdx, presetId);
  int32 enabled;
  UOSCManager::GetInt32(Message, ++msgIdx, enabled);
  int32 epoch;
  UOSCManager::GetInt32(Message, ++msgIdx, epoch);

  CommsMan->HandleAudioStream(moduleId, (uint32)streamId, presetId, enabled != 0, epoch);
}

void UOSCReceiver::RxAckAudioEnabled(const FOSCMessage& Message, const FString& IPAddress) {
  int msgIdx = -1;

  int64 moduleId;
  UOSCManager::GetInt64(Message, ++msgIdx, moduleId);
  int32 enabled;
  UOSCManager::GetInt32(Message, ++msgIdx, enabled);
  int32 success;
  UOSCManager::GetInt32(Message, ++msgIdx, success);

  CommsMan->AckAudioEnabled(moduleId, enabled != 0, success != 0);
}

void UOSCReceiver::RxAckAudioPreset(const FOSCMessage& Message, const FString& IPAddress) {
  int msgIdx = -1;

  int64 moduleId;
  UOSCManager::GetInt64(Message, ++msgIdx, moduleId);
  int32 presetId;
  UOSCManager::GetInt32(Message, ++msgIdx, presetId);
  int32 success;
  UOSCManager::GetInt32(Message, ++msgIdx, success);

  CommsMan->AckAudioPreset(moduleId, presetId, success != 0);
}
```

- [ ] **Step 3: Declare the audio state in `ACommunicationsManager`**

`Comms/CommunicationsManager.h`: add the forward declaration after
`class UStateStore;`:

```cpp
class ARSNetAudioSpeaker;
```

After `void AckLightSubscription(int64 ModuleId, bool Success);`:

```cpp

  // bCast audio (managed by OSCctrl)
  void HandleAudioStream(
    int64 ModuleId,
    uint32 StreamId,
    int32 PresetId,
    bool bEnabled,
    int32 Epoch
  );
  void AckAudioEnabled(int64 ModuleId, bool bEnabled, bool bSuccess);
  void AckAudioPreset(int64 ModuleId, int32 PresetId, bool bSuccess);
```

After the `TextureRequests` member block (the first `private:` section):

```cpp

private: // audio
  enum class EAudioState : uint8 { Idle, Requested, Playing };

  UPROPERTY()
  TObjectPtr<ARSNetAudioSpeaker> Speaker;

  EAudioState AudioState = EAudioState::Idle;
  int64 AudioModuleId = INDEX_NONE;
  uint32 AudioStreamId = 0;
  int32 AudioPresetId = INDEX_NONE;    // as announced by Rack
  int32 AudioPresetSent = INDEX_NONE;  // last /set/audio/preset request
  FTimerHandle hAudioTimer;

  ARSNetAudioSpeaker* EnsureSpeaker();
  void StartAudioTimer();
  // every 500 ms: stats, and the rs.NetAudio.Preset console variable
  void AudioTick();
  void StopAudio(bool bDestroySpeaker = false);
```

- [ ] **Step 4: Implement it in `CommunicationsManager.cpp`**

Includes, after `#include "Comms/TextureRequestManager.h"`:

```cpp
#include "Comms/Audio/RSNetAudioProtocol.h"
#include "Comms/Audio/RSNetAudioSpeaker.h"
#include "RSLogSubsystem.h"

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarNetAudioPreset(
  TEXT("rs.NetAudio.Preset"),
  RSNetAudio::DefaultPresetId,
  TEXT("bCast latency preset: 0 lowest latency, 1 balanced, 2 robust, 3 uncompressed"),
  ECVF_Default
);
```

`Stop()`: add at the top, before `ClearTimer(hKeepaliveTimer)`:

```cpp
  if (AudioState != EAudioState::Idle) {
    osctx->TxSetAudioEnabled(AudioModuleId, false, 0);
  }
  StopAudio(true);
```

`RegisterClient()`: in the `rg->OnFail` lambda, after
`ClearTimer(hKeepaliveTimer);`:

```cpp
      StopAudio(true);
```

At the end of the file:

```cpp

ARSNetAudioSpeaker* ACommunicationsManager::EnsureSpeaker() {
  if (!Speaker) {
    FActorSpawnParameters spawnParams;
    spawnParams.Owner = this;
    Speaker = GetWorld()->SpawnActor<ARSNetAudioSpeaker>(spawnParams);
  }
  return Speaker;
}

void ACommunicationsManager::StartAudioTimer() {
  FTimerManager& timers = GetWorld()->GetTimerManager();
  if (timers.IsTimerActive(hAudioTimer)) return;
  timers.SetTimer(hAudioTimer, this, &ACommunicationsManager::AudioTick, 0.5f, true);
}

void ACommunicationsManager::HandleAudioStream(
  int64 ModuleId,
  uint32 StreamId,
  int32 PresetId,
  bool bEnabled,
  int32 Epoch
) {
  if (!bEnabled) {
    // An offer: on registration, on attach, or for a bCast that just
    // detached. Ask for it. A detached module fails the request, and the
    // failed ack stops playback.
    if (AudioState != EAudioState::Idle) StopAudio();

    ARSNetAudioSpeaker* speaker = EnsureSpeaker();
    if (!speaker || !speaker->Play(StreamId)) {
      UE_LOG(LogRackSim, Error, TEXT("NetAudio: can't play stream %u"), StreamId);
      return;
    }
    AudioModuleId = ModuleId;
    AudioStreamId = StreamId;
    AudioPresetId = PresetId;
    AudioState = EAudioState::Requested;
    osctx->TxSetAudioEnabled(ModuleId, true, RSNetAudio::DefaultPort);
    StartAudioTimer();
    return;
  }

  ARSNetAudioSpeaker* speaker = EnsureSpeaker();
  if (!speaker) return;
  const bool bNewStream =
    AudioState == EAudioState::Idle || StreamId != AudioStreamId;
  if ((bNewStream || !speaker->IsPlaying()) && !speaker->Play(StreamId)) {
    UE_LOG(LogRackSim, Error, TEXT("NetAudio: can't play stream %u"), StreamId);
    StopAudio();
    return;
  }
  AudioModuleId = ModuleId;
  AudioStreamId = StreamId;
  AudioPresetId = PresetId;
  AudioState = EAudioState::Playing;
  StartAudioTimer();
}

void ACommunicationsManager::AckAudioEnabled(
  int64 ModuleId,
  bool bEnabled,
  bool bSuccess
) {
  if (bSuccess || !bEnabled || ModuleId != AudioModuleId) return;
  UE_LOG(LogRackSim, Warning, TEXT("NetAudio: Rack refused stream %lld"), ModuleId);
  StopAudio();
}

void ACommunicationsManager::AckAudioPreset(
  int64 ModuleId,
  int32 PresetId,
  bool bSuccess
) {
  if (bSuccess) return;
  UE_LOG(LogRackSim, Warning, TEXT("NetAudio: Rack refused preset %d"), PresetId);
}

void ACommunicationsManager::AudioTick() {
  if (AudioState != EAudioState::Playing || !Speaker) return;

  FRSJitterBuffer::FStats stats = Speaker->GetStats();
  osctx->TxAudioStats(
    AudioModuleId,
    stats.Underruns,
    stats.Lost,
    stats.Recovered,
    stats.JitterMs,
    stats.BufferMs
  );

  const int32 preset = CVarNetAudioPreset.GetValueOnGameThread();
  if (
    preset != AudioPresetId
      && preset != AudioPresetSent
      && RSNetAudio::FindPreset(preset)
  ) {
    AudioPresetSent = preset;
    osctx->TxSetAudioPreset(AudioModuleId, preset);
  }
}

void ACommunicationsManager::StopAudio(bool bDestroySpeaker) {
  if (UWorld* world = GetWorld()) world->GetTimerManager().ClearTimer(hAudioTimer);
  if (Speaker) {
    Speaker->StopPlayback();
    if (bDestroySpeaker) {
      Speaker->Destroy();
      Speaker = nullptr;
    }
  }
  AudioState = EAudioState::Idle;
  AudioModuleId = INDEX_NONE;
  AudioStreamId = 0;
  AudioPresetId = INDEX_NONE;
  AudioPresetSent = INDEX_NONE;
}
```

`AudioPresetSent` stops the timer from repeating a request every 500 ms
while Rack's re-announce is in flight. If Rack refuses a preset, the
console variable has to change again before RackSim retries.

- [ ] **Step 5: Build**

The user builds RackSim (Win64 editor and Android). Expected: it compiles,
and the 10 `RackSim.NetAudio` tests still pass.

- [ ] **Step 6: Live check (the user runs it)**

Rack on Windows (BENCH build from Task 9) with a VCO into a bCast directly
right of OSCctrl. RackSim in PIE, then on Quest:
1. Connect. Expect:
   - the bCast light goes full blue and the VCO is audible within about 1 s;
   - the bCast menu shows "Managed by OSCctrl" and a client stats line that
     updates about twice a second: 0 underruns, jitter of a few ms, buffer
     near 40 ms.
2. `rs.NetAudio.Preset 0`, then 2, then 3. Each time, expect a short gap,
   the bCast menu shows the new preset, and the stats buffer moves to about
   20, 80 and 20 ms. `rs.NetAudio.Preset 1` restores Balanced.
3. Open several overlays while listening. Audio stays clean; on Wi-Fi the
   stats may show jitter, but no repeated underruns.
4. Drag bCast away from OSCctrl. Expect silence within about 1 s, a
   `NetAudio: Rack refused stream` warning in the log, and the light off (or
   green if Stream is on in its own menu). Drag it back: audio returns.
5. Delete bCast, then undo: audio returns (the undo restores the module id).
6. Quit Rack. After the heartbeat timeout, RackSim shows the connection
   lost, the speaker is destroyed, and port 7747 is free (reconnecting to a
   restarted Rack plays again).
7. Connect again from the RackSim menu (`Connect` runs `ResetSession`,
   which calls `Stop()`). Expect a short gap: RackSim sends
   `/set/audio/enabled 0`, re-registers, gets a fresh offer, and audio
   returns, with the stats counts starting again from 0.

- [ ] **Step 7: Stop.** The user reviews and commits (in RackSim).

---

## Task 14: End-to-end verification and the PERFLOG

**Files:**
- Modify: `tools/bench/PERFLOG.md`
- Possibly modify: `dep.mk` or `Makefile` (CI fallback),
  `RackSim/Config/DefaultEngine.ini` (Quest latency)

**Interfaces:** none new. This task measures Tasks 1–13 together and fixes
only what the measurements show.

Follow `tools/bench/AGENTS.md` for every A/B: same session, same machine
power state, and alternate runs.

- [ ] **Step 1: CI on all four platforms**

The user pushes the branch. Then:

```bash
gh run list --branch <branch> --limit 3
gh run view <run id> --log-failed
```

Expected: lin-x64, win-x64, mac-x64 and mac-arm64 all build, and each
`dist/*.vcvplugin` contains `plugin.{so,dll,dylib}` with libopus linked
statically. Check that no libopus shared object is a dependency:
- Linux: `objdump -p plugin.so | grep NEEDED`;
- Windows: `objdump -p plugin.dll | grep "DLL Name"` (expect `qwave.dll`, no
  opus);
- macOS: `otool -L plugin.dylib`.

If a job fails in the libopus rule:
1. If cmake is missing from that toolchain image, switch the rule to
   opus's autotools build (`./configure --disable-shared --enable-static
   --disable-doc --disable-extra-programs --with-pic`, plus `--host` from
   the toolchain's `CC` triple), keeping the same tarball and checksum.
2. If that also fails, fall back to a git submodule at `dep/opus` built
   with the same flags (the spec's fallback).
3. If only Windows fails on `qos2.h` or `-lqwave`, compile DSCP marking out
   for that target (`BCAST_NO_QWAVE`); the stream still works unmarked.

Rerun until all four pass.

- [ ] **Step 2: Wi-Fi A/B (the user runs it, client on Wi-Fi)**

Rack runs on the usual PC. `audio_bench.py` runs on a laptop on the same
Wi-Fi network the Quest uses, with a sine into bCast and a Scope with
overlays. Same session, alternating runs, each 60 s:

```bat
python audio_bench.py run --preset 1 --overlays 3  --duration 60 --label bal-3-on   --json wifi-b3-on.json
python audio_bench.py run --preset 1 --overlays 3  --duration 60 --label bal-3-off  --no-chunk-pacing --json wifi-b3-off.json
python audio_bench.py run --preset 1 --overlays 16 --duration 60 --label bal-16-on  --json wifi-b16-on.json
python audio_bench.py run --preset 1 --overlays 16 --duration 60 --label bal-16-off --no-chunk-pacing --json wifi-b16-off.json
python audio_bench.py run --preset 0 --overlays 16 --duration 60 --label low-16-on  --json wifi-l16-on.json
python audio_bench.py run --preset 2 --overlays 16 --duration 60 --label rob-16-on  --json wifi-r16-on.json
python audio_bench.py compare wifi-b16-off.json wifi-b16-on.json
python audio_bench.py compare wifi-b3-off.json wifi-b3-on.json
```

Targets:
- **Balanced, 16 overlays, pacing on:** 0 simulated underruns and 0 frames
  lost after redundancy.
- **Overlay fps, pacing on vs off:** within 15% at 16 overlays.
- **3 overlays:** pacing on and off within noise (pacing should rarely
  engage).
- **Lowest latency and Robust:** record the results. They aren't
  pass/fail. They show where each preset stops being clean on this
  network.

If Balanced underruns with pacing on:
- check `tx.pacing.rate_mbps` in the result: if it never drops, the
  trouble thresholds are too loose;
- check `audio.client.jitter_ms` against the arrival jitter p99: if p99
  is above 40 ms, the bursts come from the network, not from our sends
  (compare with `--overlays 0`).

Use the systematic-debugging skill before changing any pacer constant, and
re-run the same A/B after each change.

- [ ] **Step 3: UE tests**

The user runs `RackSim.NetAudio` once more on the final RackSim build.
Expected: 10 tests pass.

- [ ] **Step 4: Quest playback and latency (the user runs it)**

1. **Soak:** 10 minutes of normal use on Quest (patching, overlays open,
   moving around). Expect no audible dropouts, and the bCast menu's
   underrun count stays at 0 or grows only during visible Wi-Fi trouble.
2. **Latency:** patch a 1 Hz click (a clock or a short envelope) into both
   bCast and a Core Audio-2 module playing on the PC's speakers. Put a
   phone recording next to the PC speaker and the Quest. In Audacity,
   measure the offset between each pair of clicks over 10 clicks. Quest
   latency = offset + the PC output latency (Rack's block size ÷ rate,
   plus the driver's).
3. **Budget:** with Balanced, expect roughly:
   - 10 ms frame and encode;
   - 40 ms jitter buffer;
   - a few ms network;
   - **about 93 ms of Android output**, because RackSim's Android section
     (`[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]`) sets
     `AudioCallbackBufferFrameSize=1024` with `AudioNumBuffersToEnqueue=4`
     at 44.1 kHz.

   If the Android output dominates, try `AudioCallbackBufferFrameSize=256`
   and `AudioNumBuffersToEnqueue=2` (and `AudioSampleRate=48000`, Quest's
   native rate). Re-measure, and repeat the soak to check for crackles from
   the smaller blocks. Record both settings; keep the new ones only if the
   soak stays clean.

- [ ] **Step 5: Write the PERFLOG "Audio" section**

`tools/bench/PERFLOG.md`: add a section after "Connection storm on large
patches", filled in from Steps 2 and 4:

```markdown
## Audio (bCast)

bCast streams Opus or PCM over its own UDP socket, marked DSCP EF / WMM
voice (see `API.md`, "Audio packet format"). Texture chunks share the
Wi-Fi link, so OSCctrl paces them from the client's `/audio/stats`
(`ChunkPacer`: AIMD on underruns, losses and jitter spikes; unlimited when
clean). A/B: `/bench/chunk_pacing`, or `audio_bench.py --no-chunk-pacing`.

Wi-Fi, client on <laptop / network>, 60 s runs, same session:

| Run | preset | overlays | pacing | underruns | lost after redundancy | jitter p95 / p99 | overlay fps | `tx.pacing.rate_mbps` min |
|---|---|---|---|---|---|---|---|---|
| `wifi-b3-off` | Balanced | 3 | off | | | | | |
| `wifi-b3-on` | Balanced | 3 | on | | | | | |
| `wifi-b16-off` | Balanced | 16 | off | | | | | |
| `wifi-b16-on` | Balanced | 16 | on | | | | | |
| `wifi-l16-on` | Lowest latency | 16 | on | | | | | |
| `wifi-r16-on` | Robust | 16 | on | | | | | |

Quest latency, click test (Balanced): <n> ms with the default Android audio
buffers (1024 × 4 at 44.1 kHz); <n> ms with <settings>, if changed.

**Remaining:** <what the numbers show is left: e.g. preset targets, pacer
thresholds, the Android buffer, an Advanced menu>.
```

Fill in every cell from the result files. Don't leave placeholders in the
committed text. If a run wasn't possible, say so in the row.

- [ ] **Step 6: Stop.** The user reviews and commits.
