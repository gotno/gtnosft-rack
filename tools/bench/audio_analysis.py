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
