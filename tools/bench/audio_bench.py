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
