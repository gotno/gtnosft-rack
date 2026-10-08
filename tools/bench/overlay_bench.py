#!/usr/bin/env python3
"""Overlay render benchmark client for OSCctrl.

Requires the plugin to be built with benchmark instrumentation
(`./build-wsl.sh BENCH=1` / `./build-msys.sh BENCH=1`). See README.md.
"""

import argparse
import json
import math
import random
import socket
import sys
import threading
import time
from collections import defaultdict
from dataclasses import dataclass, field

from pythonosc.osc_message_builder import OscMessageBuilder
from pythonosc.osc_packet import OscPacket, ParseError

# must match src/osc/OscConstants.hpp
CLIENT_PORT = 7746  # server sends (and broadcasts /announce) here
DEFAULT_SERVER_PORT = 7225

STAGES = [
    "queue_wait",
    "prepare",
    "draw",
    "readback",
    "flip",
    "handoff",
    "compress",
    "send_queue",
    "send_all",
    "ack_all",
    "render_total",
    "request_to_first_send",
    "request_to_all_acked",
    "raw_kb",
    "compressed_kb",
    "chunks",
]
KINDS = ["overlay_hit", "overlay_miss", "texture"]
HEADLINE = [
    "overlay_hit.request_to_first_send",
    "overlay_hit.render_total",
    "overlay_hit.draw",
    "overlay_hit.readback",
    "overlay_hit.compress",
    "overlay_hit.request_to_all_acked",
    "overlay_miss.request_to_first_send",
    "frame.interval",
    "frame.ctrl_step",
    "client.request_to_first_chunk",
    "client.request_to_complete",
]


class BenchError(Exception):
    pass


# --------------------------------------------------------------------------
# transport


def build(address, *args):
    """args are (type, value) pairs; types: h=int64 i=int32 f=float s=string"""
    builder = OscMessageBuilder(address=address)
    for arg_type, value in args:
        builder.add_arg(value, arg_type)
    return builder.build().dgram


@dataclass
class Frame:
    seq: int
    requested_at: float
    first_chunk_at: float = None
    completed_at: float = None
    num_chunks: int = None
    total_size: int = None
    width: int = None
    height: int = None
    chunks: dict = field(default_factory=dict)
    duplicate_chunks: int = 0
    valid: bool = None


class Client:
    def __init__(self, verbose=False):
        self.verbose = verbose
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
        try:
            self.sock.bind(("0.0.0.0", CLIENT_PORT))
        except OSError as e:
            raise BenchError(
                f"can't bind UDP {CLIENT_PORT} ({e}). is another OSCctrl client running?"
            )
        self.sock.settimeout(0.2)
        self.server = None

        self.cond = threading.Condition()
        self.running = True

        self.announce = None
        self.heartbeats = 0
        self.module_stubs = []
        self.module_count = None
        self.module_state = {}
        self.reset_ack = None
        self.report = None
        self.report_building = None
        self.frames = {}  # (textureId, seq) -> Frame

        self.thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.thread.start()
        self.keepalive_thread = None

    def close(self):
        self.running = False
        self.thread.join(timeout=1)
        if self.keepalive_thread:
            self.keepalive_thread.join(timeout=2)
        self.sock.close()

    def send(self, address, *args):
        self.sock.sendto(build(address, *args), self.server)

    def wait_for(self, predicate, timeout):
        deadline = time.monotonic() + timeout
        with self.cond:
            while not predicate():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return False
                self.cond.wait(remaining)
            return True

    # receive

    def _receive_loop(self):
        while self.running:
            try:
                data, addr = self.sock.recvfrom(65536)
            except socket.timeout:
                continue
            except OSError:
                return
            received_at = time.perf_counter()
            try:
                packet = OscPacket(data)
            except ParseError as e:
                if self.verbose:
                    print(f"  ! unparseable packet from {addr}: {e}", file=sys.stderr)
                continue
            with self.cond:
                for timed in packet.messages:
                    self._dispatch(timed.message, addr, received_at)
                self.cond.notify_all()

    def _dispatch(self, msg, addr, received_at):
        a, p = msg.address, msg.params
        if a == "/announce":
            if self.announce is None:
                self.announce = {"host": addr[0], "port": p[0], "ctrl_id": p[2], "patch": p[3]}
        elif a == "/heartbeat":
            self.heartbeats += 1
        elif a == "/set/module_stub":
            self.module_stubs.append({"id": p[0], "plugin": p[1], "module": p[2]})
        elif a == "/report/module/count":
            self.module_count = p[0]
        elif a == "/set/s/m":
            self.module_state[p[0]] = {"texture_id": p[3]}
        elif a == "/bench/reset/ack":
            self.reset_ack = p[0]
        elif a == "/bench/report/begin":
            self.report_building = {
                "generation": p[0],
                "window_sec": p[1],
                "stats": {},
                "counters": {},
                "textures": [],
                "messages": 1,
            }
        elif a.startswith("/bench/") and self.report_building is not None:
            r = self.report_building
            r["messages"] += 1
            if a == "/bench/stat":
                keys = ["count", "mean", "min", "p50", "p95", "p99", "max"]
                r["stats"][p[0]] = dict(zip(keys, p[1:]))
            elif a == "/bench/counter":
                r["counters"][p[0]] = p[1]
            elif a == "/bench/texture":
                r["textures"].append(
                    {"texture_id": p[0], "kind": p[1], "completed": p[2], "failed": p[3], "fps": p[4]}
                )
            elif a == "/bench/report/end":
                r["complete"] = r.pop("messages") == p[0]
                self.report = r
                self.report_building = None
        elif a == "/set/texture":
            self._on_chunk(p, received_at)

    def _on_chunk(self, p, received_at):
        texture_id, seq, chunk_num, num_chunks, chunk_size, total_size, width, height, blob = p
        # always ack, including duplicates from retries
        self.send("/ack_chunk", ("h", texture_id), ("i", seq), ("i", chunk_num))

        frame = self.frames.get((texture_id, seq))
        if frame is None or frame.completed_at is not None:
            return
        if frame.first_chunk_at is None:
            frame.first_chunk_at = received_at
            frame.num_chunks, frame.total_size = num_chunks, total_size
            frame.width, frame.height = width, height
        if chunk_num in frame.chunks:
            frame.duplicate_chunks += 1
            return
        frame.chunks[chunk_num] = blob
        if len(frame.chunks) == num_chunks:
            frame.completed_at = received_at
            data = b"".join(frame.chunks[i] for i in range(num_chunks))
            frame.chunks = {}
            frame.valid = (
                len(data) == total_size
                and data[:4] == b"qoif"
                and int.from_bytes(data[4:8], "big") == width
                and int.from_bytes(data[8:12], "big") == height
            )

    # protocol steps

    def discover(self, timeout):
        print(f"listening for /announce on :{CLIENT_PORT} for {timeout:.0f}s...")
        if self.wait_for(lambda: self.announce is not None, timeout):
            a = self.announce
            print(f"  found OSCctrl at {a['host']}:{a['port']} (patch: {a['patch']})")
            return a["host"], a["port"]
        return None

    def register(self, host, port):
        self.server = (host, port)
        start = self.heartbeats
        self.send("/register")
        if not self.wait_for(lambda: self.heartbeats > start, 3):
            raise BenchError(
                f"no /heartbeat from {host}:{port} after /register.\n"
                "  - another client may already be registered (it must disconnect, or wait\n"
                "    ~5s after it stops sending keepalives)\n"
                "  - the Windows firewall may be blocking UDP to this machine on port 7746"
            )
        self.keepalive_thread = threading.Thread(target=self._keepalive_loop, daemon=True)
        self.keepalive_thread.start()

    def _keepalive_loop(self):
        while self.running:
            try:
                self.send("/keepalive")
            except OSError:
                return
            time.sleep(1.0)

    def bench_reset(self):
        self.reset_ack = None
        self.send("/bench/reset")
        if not self.wait_for(lambda: self.reset_ack is not None, 3):
            raise BenchError(
                "no /bench/reset/ack. is the plugin built with BENCH=1? "
                "(Rack's log will show 'no route for address /bench/reset')"
            )

    def bench_report(self):
        self.report = None
        self.send("/bench/report")
        if not self.wait_for(lambda: self.report is not None, 5):
            raise BenchError("no complete /bench/report received")
        if not self.report.get("complete"):
            print("  ! some report packets were dropped; results are partial", file=sys.stderr)
        return self.report

    def find_module(self, plugin, module, index, module_id):
        self.module_stubs, self.module_count = [], None
        self.send("/get/module_stubs")
        if not self.wait_for(lambda: self.module_count is not None, 5):
            raise BenchError("no module stubs received")
        if module_id is not None:
            matches = [m for m in self.module_stubs if m["id"] == module_id]
        else:
            matches = [
                m for m in self.module_stubs if m["plugin"] == plugin and m["module"] == module
            ]
        if len(matches) <= index:
            available = sorted({f"{m['plugin']}:{m['module']}" for m in self.module_stubs})
            raise BenchError(
                f"module not found (wanted {module_id or f'{plugin}:{module}'} #{index}). "
                f"patch has: {', '.join(available)}"
            )
        target = matches[index]

        self.send("/get/module_state", ("h", target["id"]))
        if not self.wait_for(lambda: target["id"] in self.module_state, 5):
            raise BenchError(f"no module state for {target['id']}")
        target["texture_id"] = self.module_state[target["id"]]["texture_id"]
        return target

    def request_texture(self, texture_id, seq, size_args):
        with self.cond:
            self.frames[(texture_id, seq)] = Frame(seq=seq, requested_at=time.perf_counter())
        self.send("/get/texture", ("h", texture_id), ("i", seq), *size_args)


# --------------------------------------------------------------------------
# run


def wsl_default_gateway():
    try:
        with open("/proc/version") as f:
            if "microsoft" not in f.read().lower():
                return None
        with open("/proc/net/route") as f:
            for line in f.readlines()[1:]:
                fields = line.split()
                if fields[1] == "00000000":
                    return socket.inet_ntoa(int(fields[2], 16).to_bytes(4, "little"))
    except OSError:
        pass
    return None


def stream(client, texture_id, size_args, rate, duration, timeout):
    """Request frames for `duration` seconds. rate=0 is closed loop: request the
    next frame as soon as the previous one completes (or times out)."""
    seq = random.randint(1, 1_000_000_000)
    first_seq = seq
    end = time.perf_counter() + duration

    if rate > 0:
        interval = 1.0 / rate
        next_at = time.perf_counter()
        while (now := time.perf_counter()) < end:
            if now < next_at:
                time.sleep(min(next_at - now, 0.002))
                continue
            client.request_texture(texture_id, seq, size_args)
            seq += 1
            next_at += interval
            if next_at < now - interval:  # fell behind, don't burst to catch up
                next_at = now
    else:
        while time.perf_counter() < end:
            key = (texture_id, seq)
            client.request_texture(texture_id, seq, size_args)
            client.wait_for(lambda: client.frames[key].completed_at is not None, timeout)
            seq += 1

    # drain in-flight frames
    keys = [(texture_id, s) for s in range(first_seq, seq)]
    client.wait_for(
        lambda: all(client.frames[k].completed_at is not None for k in keys), timeout
    )
    with client.cond:
        return [client.frames[k] for k in keys]


def summarize(values):
    if not values:
        return None
    s = sorted(values)

    def pct(p):
        return s[min(len(s) - 1, max(0, math.ceil(p * len(s)) - 1))]

    return {
        "count": len(s),
        "mean": sum(s) / len(s),
        "min": s[0],
        "p50": pct(0.50),
        "p95": pct(0.95),
        "p99": pct(0.99),
        "max": s[-1],
    }


def client_stats(frames, duration):
    done = [f for f in frames if f.completed_at is not None]
    stats = {}
    for name, values in {
        "client.request_to_first_chunk": [
            (f.first_chunk_at - f.requested_at) * 1000 for f in frames if f.first_chunk_at
        ],
        "client.request_to_complete": [(f.completed_at - f.requested_at) * 1000 for f in done],
        "client.first_to_last_chunk": [(f.completed_at - f.first_chunk_at) * 1000 for f in done],
    }.items():
        summary = summarize(values)
        if summary:
            stats[name] = summary

    completions = sorted(f.completed_at for f in done)
    fps = (
        (len(completions) - 1) / (completions[-1] - completions[0])
        if len(completions) >= 2 and completions[-1] > completions[0]
        else 0.0
    )
    sizes = {(f.width, f.height) for f in done}
    return {
        "stats": stats,
        "counters": {
            "client.requested": len(frames),
            "client.completed": len(done),
            "client.incomplete": len(frames) - len(done),
            "client.invalid": sum(1 for f in done if not f.valid),
            "client.duplicate_chunks": sum(f.duplicate_chunks for f in frames),
        },
        "fps": fps,
        "requested_fps": len(frames) / duration if duration else 0.0,
        "sizes": sorted(f"{w}x{h}" for w, h in sizes),
    }


def cmd_run(args):
    if args.scale is not None:
        size_args = [("f", float(args.scale))]
    else:
        size_args = [("i", args.height)]
        if args.width:
            size_args.append(("i", args.width))

    client = Client(verbose=args.verbose)
    try:
        if args.host:
            host, port = args.host, args.port
        else:
            found = client.discover(args.discover_timeout)
            if found:
                host, port = found
            else:
                host, port = wsl_default_gateway(), args.port
                if not host:
                    raise BenchError("no /announce received; pass --host")
                print(f"  no /announce (normal under WSL NAT); trying Windows host {host}:{port}")

        client.register(host, port)
        print(f"registered with {host}:{port}")
        client.bench_reset()

        target = client.find_module(args.plugin, args.module, args.index, args.module_id)
        print(
            f"target: {target['plugin']}:{target['module']} "
            f"module {target['id']} overlay texture {target['texture_id']}"
        )
        texture_id = target["texture_id"]

        print(f"baseline: idle for {args.idle:.1f}s...")
        client.bench_reset()
        time.sleep(args.idle)
        baseline = client.bench_report()

        if args.warmup > 0:
            print(f"warmup: streaming for {args.warmup:.1f}s...")
            stream(client, texture_id, size_args, args.rate, args.warmup, args.timeout)

        mode = f"{args.rate:g} fps open loop" if args.rate > 0 else "closed loop"
        print(f"stream: {mode} for {args.duration:.1f}s...")
        client.bench_reset()
        frames = stream(client, texture_id, size_args, args.rate, args.duration, args.timeout)
        streamed = client.bench_report()
    finally:
        client.close()

    result = {
        "label": args.label,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "args": {k: v for k, v in vars(args).items() if k != "func"},
        "target": target,
        "baseline": baseline,
        "stream": streamed,
        "client": client_stats(frames, args.duration),
    }

    print_result(result)

    if args.json:
        with open(args.json, "w") as f:
            json.dump(result, f, indent=2)
        print(f"\nwrote {args.json}")

    return 0


# --------------------------------------------------------------------------
# output


def fmt(v):
    if v is None:
        return "-"
    return f"{v:.3f}" if abs(v) < 10 else f"{v:.1f}"


STAT_HEADER = f"  {'':40} {'n':>6} {'mean':>8} {'p50':>8} {'p95':>8} {'p99':>8} {'max':>8}"


def stat_row(name, s):
    return (
        f"  {name:40} {s['count']:>6} {fmt(s['mean']):>8} {fmt(s['p50']):>8} "
        f"{fmt(s['p95']):>8} {fmt(s['p99']):>8} {fmt(s['max']):>8}"
    )


def ordered_stat_names(stats):
    names = []
    for kind in KINDS:
        names += [f"{kind}.{stage}" for stage in STAGES if f"{kind}.{stage}" in stats]
    names += sorted(n for n in stats if n.startswith("frame."))
    names += sorted(n for n in stats if n not in names)
    return names


def print_result(r):
    b, s, c = r["baseline"], r["stream"], r["client"]

    print("\n== frame time (ms): idle baseline vs streaming ==")
    print(STAT_HEADER)
    for name in ["frame.interval", "frame.ctrl_step"]:
        if name in b["stats"]:
            print(stat_row(f"idle   {name}", b["stats"][name]))
        if name in s["stats"]:
            print(stat_row(f"stream {name}", s["stats"][name]))

    print(f"\n== server pipeline (ms unless noted), {s['window_sec']:.1f}s window ==")
    print(STAT_HEADER)
    for name in ordered_stat_names(s["stats"]):
        if not name.startswith("frame."):
            print(stat_row(name, s["stats"][name]))

    print("\n== client (ms) ==")
    print(STAT_HEADER)
    for name, stat in c["stats"].items():
        print(stat_row(name, stat))

    print("\n== throughput ==")
    print(f"  requested           {c['requested_fps']:.1f} fps")
    print(f"  client completed    {c['fps']:.1f} fps")
    for t in s["textures"]:
        print(
            f"  server {t['kind']} {t['texture_id']}: {t['fps']:.1f} fps "
            f"({t['completed']} completed, {t['failed']} failed)"
        )
    print(f"  image sizes         {', '.join(c['sizes']) or '-'}")

    print("\n== counters ==")
    counters = {**s["counters"], **c["counters"]}
    for name in sorted(counters):
        print(f"  {name:40} {counters[name]}")


# --------------------------------------------------------------------------
# compare


def flatten(result):
    stats = {}
    for name, stat in result["stream"]["stats"].items():
        stats[name] = stat
    for name, stat in result["client"]["stats"].items():
        stats[name] = stat
    for name, stat in result["baseline"]["stats"].items():
        stats[f"idle {name}"] = stat
    stats["client.fps"] = {"p50": result["client"]["fps"], "p95": None}
    return stats


def cmd_compare(args):
    with open(args.before) as f:
        before = json.load(f)
    with open(args.after) as f:
        after = json.load(f)
    a, b = flatten(before), flatten(after)

    names = HEADLINE + ["client.fps"] if not args.all else ordered_stat_names({**a, **b})
    label_a = before.get("label") or args.before
    label_b = after.get("label") or args.after
    print(f"before: {label_a}\nafter:  {label_b}\n")
    print(
        f"  {'':40} {'p50 before':>11} {'p50 after':>10} {'Δ':>8}   "
        f"{'p95 before':>11} {'p95 after':>10} {'Δ':>8}"
    )

    def delta(x, y):
        if x is None or y is None or x == 0:
            return "-"
        return f"{(y - x) / x * 100:+.0f}%"

    for name in names:
        if name not in a and name not in b:
            continue
        sa, sb = a.get(name, {}), b.get(name, {})
        print(
            f"  {name:40} {fmt(sa.get('p50')):>11} {fmt(sb.get('p50')):>10} "
            f"{delta(sa.get('p50'), sb.get('p50')):>8}   "
            f"{fmt(sa.get('p95')):>11} {fmt(sb.get('p95')):>10} "
            f"{delta(sa.get('p95'), sb.get('p95')):>8}"
        )
    return 0


# --------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(required=True)

    run = sub.add_parser("run", help="benchmark streaming overlay renders")
    run.add_argument("--host", help="OSCctrl host (default: discover, then WSL Windows host)")
    run.add_argument("--port", type=int, default=DEFAULT_SERVER_PORT, help="OSCctrl listen port")
    run.add_argument("--discover-timeout", type=float, default=3.0)
    run.add_argument("--plugin", default="Fundamental", help="target module plugin slug")
    run.add_argument("--module", default="Scope", help="target module slug")
    run.add_argument("--index", type=int, default=0, help="which match, if several")
    run.add_argument("--module-id", type=int, help="target module id (overrides slugs)")
    size = run.add_mutually_exclusive_group()
    size.add_argument("--height", type=int, default=512, help="render height in px")
    size.add_argument("--scale", type=float, help="render scale instead of height")
    run.add_argument("--width", type=int, help="render width in px (with --height)")
    run.add_argument("--rate", type=float, default=60, help="requests/sec; 0 = closed loop")
    run.add_argument("--duration", type=float, default=10, help="measured stream seconds")
    run.add_argument("--idle", type=float, default=3, help="idle baseline seconds")
    run.add_argument("--warmup", type=float, default=1, help="unmeasured stream seconds")
    run.add_argument("--timeout", type=float, default=2, help="per-frame/drain timeout")
    run.add_argument("--label", default="", help="label stored in the JSON result")
    run.add_argument("--json", help="write results to this file")
    run.add_argument("-v", "--verbose", action="store_true")
    run.set_defaults(func=cmd_run)

    compare = sub.add_parser("compare", help="compare two JSON results")
    compare.add_argument("before")
    compare.add_argument("after")
    compare.add_argument("--all", action="store_true", help="show every stat")
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
