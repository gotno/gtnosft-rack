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
import struct
import sys
import threading
import time
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
    "handoff",
    "flip",
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
KINDS = ["overlay", "overlay_hit", "overlay_miss", "texture"]
HEADLINE = [
    "overlay.request_to_first_send",
    "overlay.queue_wait",
    "overlay.render_total",
    "overlay.prepare",
    "overlay.draw",
    "overlay.readback",
    "overlay.compress",
    "overlay.request_to_all_acked",
    "frame.interval",
    "frame.ctrl_step",
    "client.request_to_first_chunk",
    "client.request_to_complete",
]


# single values, shown in the p50 column by compare
TRANSPORT_HEADLINE = [
    "transport.acks_per_sec",
    "transport.ack_us",
    "transport.rx_busy_pct",
    "transport.tx_busy_pct",
    "transport.chunk_tick_us",
    "transport.retry_pct",
    "transport.up_packets_per_sec",
]


class BenchError(Exception):
    pass


RCVBUF_REQUEST = 8 * 1024 * 1024

# fast path for the hot /set/texture chunks; python-osc is too slow to keep up
# with large frames arriving in a burst
_TEXTURE_PREFIX = b"/set/texture\0\0\0\0,hiiiihiib\0\0"
_TEXTURE_ARGS = struct.Struct(">qiiiiqiii")  # ...width, height, blob size
_ACK_PREFIX = b"/ack_chunk\0\0,hii\0\0\0\0"
_ACK_ARGS = struct.Struct(">qii")


def parse_texture_chunks(data):
    """Returns the /set/texture chunk params in a packet (bundle or message),
    or None if the packet contains anything else."""
    if data.startswith(b"#bundle\0"):
        elements, offset = [], 16
        while offset < len(data):
            (size,) = struct.unpack_from(">i", data, offset)
            elements.append((offset + 4, offset + 4 + size))
            offset += 4 + size
    else:
        elements = [(0, len(data))]

    chunks = []
    prefix_len = len(_TEXTURE_PREFIX)
    for start, end in elements:
        if data[start:start + prefix_len] != _TEXTURE_PREFIX:
            return None
        args_at = start + prefix_len
        *params, blob_size = _TEXTURE_ARGS.unpack_from(data, args_at)
        blob_at = args_at + _TEXTURE_ARGS.size
        if blob_at + blob_size > end:
            return None
        chunks.append((*params, data[blob_at:blob_at + blob_size]))
    return chunks


TRANSPORT_COUNTERS = [
    "chunk_packets",
    "chunk_bytes",
    "acks_sent",
    "ack_bytes",
    "sim_dropped_packets",
    "sim_dropped_acks",
]


def udp_socket_drops(port):
    """Kernel receive drops for our UDP socket (Linux), or None."""
    try:
        with open("/proc/net/udp") as f:
            next(f)
            for line in f:
                fields = line.split()
                if int(fields[1].split(":")[1], 16) == port:
                    return int(fields[-1])
    except (OSError, ValueError, IndexError):
        pass
    return None


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
    superseded: bool = False


class Client:
    def __init__(self, verbose=False):
        self.verbose = verbose
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, RCVBUF_REQUEST)
        try:
            self.sock.bind(("0.0.0.0", CLIENT_PORT))
        except OSError as e:
            raise BenchError(
                f"can't bind UDP {CLIENT_PORT} ({e}). is another OSCctrl client running?"
            )
        self.sock.settimeout(0.2)
        # Linux reports double the usable size
        rcvbuf = self.sock.getsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF)
        if sys.platform.startswith("linux") and rcvbuf < RCVBUF_REQUEST * 2:
            print(
                f"warning: UDP receive buffer capped at {rcvbuf // 2048} KB "
                f"(asked for {RCVBUF_REQUEST // 1024} KB). large frames will overflow it\n"
                f"  and show up as chunk retries. raise the cap with:\n"
                f"    sudo sysctl -w net.core.rmem_max={RCVBUF_REQUEST}",
                file=sys.stderr,
            )
        self.server = None

        self.cond = threading.Condition()
        self.running = True

        self.announce = None
        self.heartbeats = 0
        self.module_stubs = []
        self.module_count = None
        self.module_state = {}
        self.reset_ack = None
        self.toggle_acks = {}  # toggle route -> acked state
        self.report = None
        self.report_building = None
        self.frames = {}  # (textureId, seq) -> Frame

        # simulated loss, applied after the packet reaches this socket
        self.drop_rate = 0.0  # incoming chunk packets discarded unacked
        self.ack_drop_rate = 0.0  # acks not sent
        # receive thread only; read via transport_snapshot()
        self.transport = dict.fromkeys(TRANSPORT_COUNTERS, 0)

        self.thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.thread.start()
        self.keepalive_thread = None

    def close(self):
        self.running = False
        self.thread.join(timeout=1)
        if self.keepalive_thread:
            self.keepalive_thread.join(timeout=2)
        self.sock.close()

    def transport_snapshot(self):
        with self.cond:
            return dict(self.transport)

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
                chunks = parse_texture_chunks(data)
            except struct.error:
                chunks = None
            if chunks is not None:
                with self.cond:
                    self.transport["chunk_packets"] += 1
                    self.transport["chunk_bytes"] += len(data)
                    if self.drop_rate and random.random() < self.drop_rate:
                        self.transport["sim_dropped_packets"] += 1
                        continue
                    for chunk in chunks:
                        self._on_chunk(chunk, received_at)
                    self.cond.notify_all()
                continue
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
        elif a in ("/bench/overlay_cache/ack", "/bench/prep_worker/ack"):
            self.toggle_acks[a[: -len("/ack")]] = bool(p[0])
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
        if self.ack_drop_rate and random.random() < self.ack_drop_rate:
            self.transport["sim_dropped_acks"] += 1
        else:
            ack = _ACK_PREFIX + _ACK_ARGS.pack(texture_id, seq, chunk_num)
            self.sock.sendto(ack, self.server)
            self.transport["acks_sent"] += 1
            self.transport["ack_bytes"] += len(ack)

        frame = self.frames.get((texture_id, seq))
        if frame is None:
            return
        if frame.completed_at is not None:
            frame.duplicate_chunks += 1
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

    def set_toggle(self, route, enabled):
        self.toggle_acks.pop(route, None)
        self.send(route, ("i", int(enabled)))
        if not self.wait_for(lambda: route in self.toggle_acks, 3):
            raise BenchError(f"no {route}/ack")
        if self.toggle_acks[route] != enabled:
            raise BenchError(f"{route} state not applied")

    def bench_report(self):
        self.report = None
        self.send("/bench/report")
        if not self.wait_for(lambda: self.report is not None, 5):
            raise BenchError("no complete /bench/report received")
        if not self.report.get("complete"):
            print("  ! some report packets were dropped; results are partial", file=sys.stderr)
        return self.report

    def find_targets(self, plugin, module, index, module_ids):
        self.module_stubs, self.module_count = [], None
        self.send("/get/module_stubs")
        if not self.wait_for(lambda: self.module_count is not None, 5):
            raise BenchError("no module stubs received")
        stubs = {m["id"]: m for m in self.module_stubs}

        if module_ids:
            missing = [i for i in module_ids if i not in stubs]
            if missing:
                raise BenchError(
                    f"module ids not in patch: {', '.join(map(str, missing))}. patch has:\n"
                    + "\n".join(
                        f"  {m['id']}  {m['plugin']}:{m['module']}" for m in self.module_stubs
                    )
                )
            targets = [dict(stubs[i]) for i in dict.fromkeys(module_ids)]
        else:
            matches = [
                m for m in self.module_stubs if m["plugin"] == plugin and m["module"] == module
            ]
            if len(matches) <= index:
                available = sorted({f"{m['plugin']}:{m['module']}" for m in self.module_stubs})
                raise BenchError(
                    f"module not found (wanted {plugin}:{module} #{index}). "
                    f"patch has: {', '.join(available)}"
                )
            targets = [dict(matches[index])]

        for target in targets:
            self.send("/get/module_state", ("h", target["id"]))
        if not self.wait_for(lambda: all(t["id"] in self.module_state for t in targets), 5):
            raise BenchError("no module state received for some targets")
        for target in targets:
            target["texture_id"] = self.module_state[target["id"]]["texture_id"]
        return targets

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


def stream(client, texture_ids, size_args, rate, duration, timeout):
    """Request frames of every texture for `duration` seconds. `rate` is per
    texture; open loop requests all textures together on each tick. rate=0 is
    closed loop: each texture requests its next frame as soon as its previous
    one completes (or times out), independently of the others.
    Returns {texture_id: [Frame, ...]}."""
    seqs = {tid: random.randint(1, 1_000_000_000) for tid in texture_ids}
    first_seqs = dict(seqs)
    end = time.perf_counter() + duration

    if rate > 0:
        interval = 1.0 / rate
        next_at = time.perf_counter()
        while (now := time.perf_counter()) < end:
            if now < next_at:
                time.sleep(min(next_at - now, 0.002))
                continue
            for tid in texture_ids:
                client.request_texture(tid, seqs[tid], size_args)
                seqs[tid] += 1
            next_at += interval
            if next_at < now - interval:  # fell behind, don't burst to catch up
                next_at = now
    else:
        in_flight = {}  # texture_id -> (key, requested_at)
        while (now := time.perf_counter()) < end:
            for tid in texture_ids:
                current = in_flight.get(tid)
                if current is not None:
                    key, requested_at = current
                    with client.cond:
                        done = client.frames[key].completed_at is not None
                    if not done and now - requested_at < timeout:
                        continue
                client.request_texture(tid, seqs[tid], size_args)
                in_flight[tid] = ((tid, seqs[tid]), now)
                seqs[tid] += 1
            time.sleep(0.0005)

    def frames_of(tid):
        return [client.frames[(tid, s)] for s in range(first_seqs[tid], seqs[tid])]

    def settled():
        mark_superseded([frames_of(tid) for tid in texture_ids])
        return all(
            f.completed_at is not None or f.superseded
            for tid in texture_ids
            for f in frames_of(tid)
        )

    # drain in-flight frames
    client.wait_for(settled, timeout)
    with client.cond:
        settled()
        return {tid: frames_of(tid) for tid in texture_ids}


def mark_superseded(frame_lists):
    """The server coalesces queued requests for the same texture and size into
    the newest one and never answers the older sequence ids. A frame that got
    nothing while a later frame of the same texture did was superseded."""
    for frames in frame_lists:
        answered = False
        for frame in reversed(frames):  # ascending seq order
            if frame.first_chunk_at is not None:
                answered = True
            elif answered:
                frame.superseded = True


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


def completed_fps(done):
    completions = sorted(f.completed_at for f in done)
    if len(completions) >= 2 and completions[-1] > completions[0]:
        return (len(completions) - 1) / (completions[-1] - completions[0])
    return 0.0


def latency_stats(frames):
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
    return stats, done


def client_stats(frames_by_texture, duration):
    frames = [f for fs in frames_by_texture.values() for f in fs]
    stats, done = latency_stats(frames)

    per_texture = {}
    for tid, fs in frames_by_texture.items():
        t_stats, t_done = latency_stats(fs)
        per_texture[str(tid)] = {
            "stats": t_stats,
            "requested": len(fs),
            "completed": len(t_done),
            "fps": completed_fps(t_done),
        }

    sizes = {(f.width, f.height) for f in done}
    return {
        "stats": stats,
        "counters": {
            "client.requested": len(frames),
            "client.completed": len(done),
            "client.superseded": sum(1 for f in frames if f.superseded),
            "client.incomplete": sum(
                1 for f in frames if f.completed_at is None and not f.superseded
            ),
            "client.invalid": sum(1 for f in done if not f.valid),
            "client.duplicate_chunks": sum(f.duplicate_chunks for f in frames),
        },
        # per texture, averaged, so runs with different overlay counts compare
        "fps": (
            sum(t["fps"] for t in per_texture.values()) / len(per_texture)
            if per_texture else 0.0
        ),
        "total_fps": completed_fps(done),
        "requested_fps": len(frames) / duration / max(1, len(frames_by_texture))
        if duration else 0.0,
        "per_texture": per_texture,
        "sizes": sorted(f"{w}x{h}" for w, h in sizes),
    }


def transport_stats(streamed, client_counters, client_sec):
    """Rates and thread load from the server tallies and client byte counts.
    Server figures use the server's measurement window, client figures the
    client's streaming time (both include the drain)."""
    c = streamed["counters"]
    window = streamed["window_sec"] or float("nan")

    def busy_pct(name):
        return c.get(f"{name}.busy_us", 0) / (window * 1e6) * 100

    def per_call_us(name):
        calls = c.get(name, 0)
        return c.get(f"{name}.busy_us", 0) / calls if calls else None

    frames = sum(t["completed"] for t in streamed["textures"])
    acks = c.get("rx.acks", 0)
    tx = c.get("tx.packets", 0)
    retries = sum(v for k, v in c.items() if k.endswith(".retries") and k.count(".") == 1)
    return {
        "acks_per_sec": acks / window,
        "acks_per_frame": acks / frames if frames else None,
        "ack_us": per_call_us("rx.acks"),
        "ack_busy_pct": busy_pct("rx.acks"),
        "rx_busy_pct": busy_pct("rx.packets"),
        "acks_unknown": c.get("rx.acks.unknown", 0),
        "acks_duplicate": c.get("rx.acks.duplicate", 0),
        "tx_packets_per_sec": tx / window,
        "tx_us": per_call_us("tx.packets"),
        "tx_busy_pct": busy_pct("tx.packets"),
        "chunk_tick_us": per_call_us("ui.chunk_tick"),
        "retries": retries,
        "retry_pct": retries / tx * 100 if tx else None,
        "down_mbps": client_counters["client.chunk_bytes"] * 8 / client_sec / 1e6,
        "up_kbps": client_counters["client.ack_bytes"] * 8 / client_sec / 1e3,
        "up_packets_per_sec": client_counters["client.acks_sent"] / client_sec,
    }


def connect(client, args):
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


def cmd_list(args):
    client = Client(verbose=args.verbose)
    try:
        connect(client, args)
        client.module_stubs, client.module_count = [], None
        client.send("/get/module_stubs")
        if not client.wait_for(lambda: client.module_count is not None, 5):
            raise BenchError("no module stubs received")
        for m in client.module_stubs:
            print(f"  {m['id']:>20}  {m['plugin']}:{m['module']}")
    finally:
        client.close()
    return 0


def cmd_run(args):
    for flag in ("drop", "drop_acks"):
        if not 0 <= getattr(args, flag) < 1:
            raise BenchError(f"--{flag.replace('_', '-')} must be in [0, 1)")
    if args.scale is not None:
        size_args = [("f", float(args.scale))]
    else:
        size_args = [("i", args.height)]
        if args.width:
            size_args.append(("i", args.width))

    client = Client(verbose=args.verbose)
    restore = []  # toggles to switch back on when the run ends
    try:
        connect(client, args)
        client.bench_reset()
        for route, enabled, name in (
            ("/bench/overlay_cache", args.overlay_cache, "overlay cache"),
            ("/bench/prep_worker", args.prep_worker, "prep worker"),
        ):
            if not enabled:
                restore.append(route)
            client.set_toggle(route, enabled)
            print(f"{name}: {'on' if enabled else 'OFF'}")

        targets = client.find_targets(args.plugin, args.module, args.index, args.module_id)
        for target in targets:
            print(
                f"target: {target['plugin']}:{target['module']} "
                f"module {target['id']} overlay texture {target['texture_id']}"
            )
        texture_ids = [t["texture_id"] for t in targets]

        client.drop_rate, client.ack_drop_rate = args.drop, args.drop_acks
        if args.drop or args.drop_acks:
            print(f"simulated loss: {args.drop:.1%} of chunk packets, {args.drop_acks:.1%} of acks")

        print(f"baseline: idle for {args.idle:.1f}s...")
        client.bench_reset()
        time.sleep(args.idle)
        baseline = client.bench_report()

        if args.warmup > 0:
            print(f"warmup: streaming for {args.warmup:.1f}s...")
            stream(client, texture_ids, size_args, args.rate, args.warmup, args.timeout)

        mode = f"{args.rate:g} fps open loop" if args.rate > 0 else "closed loop"
        count = f"{len(targets)} overlays, " if len(targets) > 1 else ""
        print(f"stream: {count}{mode} for {args.duration:.1f}s...")
        client.bench_reset()
        drops_before = udp_socket_drops(CLIENT_PORT)
        transport_before = client.transport_snapshot()
        stream_start = time.perf_counter()
        frames = stream(client, texture_ids, size_args, args.rate, args.duration, args.timeout)
        stream_sec = time.perf_counter() - stream_start
        transport_after = client.transport_snapshot()
        drops_after = udp_socket_drops(CLIENT_PORT)
        streamed = client.bench_report()
    finally:
        for route in restore:
            try:
                client.set_toggle(route, True)
            except Exception:
                print(f"  ! could not re-enable {route}", file=sys.stderr)
        client.close()

    result = {
        "label": args.label,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "args": {k: v for k, v in vars(args).items() if k != "func"},
        "targets": targets,
        "baseline": baseline,
        "stream": streamed,
        "client": client_stats(frames, args.duration),
    }
    if drops_before is not None and drops_after is not None:
        result["client"]["counters"]["client.socket_drops"] = drops_after - drops_before
    for name in TRANSPORT_COUNTERS:
        result["client"]["counters"][f"client.{name}"] = transport_after[name] - transport_before[name]
    result["transport"] = transport_stats(streamed, result["client"]["counters"], stream_sec)

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
    stats = s["stats"]
    # with only hits or only misses, the split rows duplicate overlay.*
    mixed = any(n.startswith("overlay_hit.") for n in stats) and any(
        n.startswith("overlay_miss.") for n in stats
    )
    for name in ordered_stat_names(stats):
        if name.startswith("frame."):
            continue
        if not mixed and name.startswith(("overlay_hit.", "overlay_miss.")):
            continue
        print(stat_row(name, stats[name]))

    print("\n== client (ms) ==")
    print(STAT_HEADER)
    for name, stat in c["stats"].items():
        print(stat_row(name, stat))

    per_texture = c.get("per_texture", {})
    if len(per_texture) > 1:
        names = {str(t["texture_id"]): f"{t['plugin']}:{t['module']} {t['id']}" for t in r["targets"]}
        print("\n== per overlay (client) ==")
        print(
            f"  {'':40} {'fps':>6} {'done':>6} {'first p50':>10} {'first p95':>10} "
            f"{'done p50':>9} {'done p95':>9}"
        )
        for tid, t in per_texture.items():
            first = t["stats"].get("client.request_to_first_chunk", {})
            whole = t["stats"].get("client.request_to_complete", {})
            print(
                f"  {names.get(tid, tid):40} {t['fps']:>6.1f} "
                f"{t['completed']:>3}/{t['requested']:<3}"
                f"{fmt(first.get('p50')):>10} {fmt(first.get('p95')):>10} "
                f"{fmt(whole.get('p50')):>9} {fmt(whole.get('p95')):>9}"
            )

    print("\n== throughput ==")
    print(f"  requested           {c['requested_fps']:.1f} fps per overlay")
    print(f"  client completed    {c['fps']:.1f} fps per overlay (avg)")
    if len(per_texture) > 1:
        print(f"  client total        {c['total_fps']:.1f} fps")
    for t in s["textures"]:
        print(
            f"  server {t['kind']} {t['texture_id']}: {t['fps']:.1f} fps "
            f"({t['completed']} completed, {t['failed']} failed)"
        )
    print(f"  image sizes         {', '.join(c['sizes']) or '-'}")

    t = r.get("transport")
    if t:
        print("\n== transport ==")
        rows = [
            ("acks/s received", t["acks_per_sec"], ""),
            ("acks per completed frame", t["acks_per_frame"], ""),
            ("ack handling, per ack", t["ack_us"], "us"),
            ("ack handling, rx thread busy", t["ack_busy_pct"], "%"),
            ("rx thread busy (all packets)", t["rx_busy_pct"], "%"),
            ("acks for finished sends", t["acks_unknown"], ""),
            ("acks for already-acked chunks", t["acks_duplicate"], ""),
            ("packets/s sent", t["tx_packets_per_sec"], ""),
            ("send call, per packet", t["tx_us"], "us"),
            ("tx thread busy in send calls", t["tx_busy_pct"], "%"),
            ("chunk tick (UI thread), per frame", t["chunk_tick_us"], "us"),
            ("chunks resent", t["retries"], ""),
            ("chunks resent, % of packets sent", t["retry_pct"], "%"),
            ("downstream", t["down_mbps"], "Mbit/s"),
            ("upstream (acks)", t["up_kbps"], "kbit/s"),
            ("upstream packets/s", t["up_packets_per_sec"], ""),
        ]
        for name, value, unit in rows:
            print(f"  {name:40} {fmt(value):>10} {unit}")

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
    for name, value in result.get("transport", {}).items():
        stats[f"transport.{name}"] = {"p50": value, "p95": None}

    # results from before the combined overlay.* stats existed: derive them
    # when the run was all hits or all misses
    if not any(n.startswith("overlay.") for n in stats):
        hit = any(n.startswith("overlay_hit.") for n in stats)
        miss = any(n.startswith("overlay_miss.") for n in stats)
        if hit != miss:
            prefix = "overlay_hit." if hit else "overlay_miss."
            for name in [n for n in stats if n.startswith(prefix)]:
                stats["overlay." + name[len(prefix):]] = stats[name]
    return stats


def cmd_compare(args):
    with open(args.before) as f:
        before = json.load(f)
    with open(args.after) as f:
        after = json.load(f)
    a, b = flatten(before), flatten(after)

    names = (
        HEADLINE + ["client.fps"] + TRANSPORT_HEADLINE
        if not args.all
        else ordered_stat_names({**a, **b})
    )
    def describe(result, path):
        label = result.get("label") or path
        notes = []
        n = len(result.get("targets", [result.get("target")]))
        if n > 1:
            notes.append(f"{n} overlays")
        if result.get("args", {}).get("overlay_cache") is False:
            notes.append("overlay cache off")
        if result.get("args", {}).get("prep_worker") is False:
            notes.append("inline prep")
        for flag, what in (("drop", "chunks"), ("drop_acks", "acks")):
            rate = result.get("args", {}).get(flag)
            if rate:
                notes.append(f"{rate:.1%} {what} dropped")
        return label + (f" ({', '.join(notes)})" if notes else "")

    label_a = describe(before, args.before)
    label_b = describe(after, args.after)
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

    def add_connection_args(p):
        p.add_argument("--host", help="OSCctrl host (default: discover, then WSL Windows host)")
        p.add_argument("--port", type=int, default=DEFAULT_SERVER_PORT, help="OSCctrl listen port")
        p.add_argument("--discover-timeout", type=float, default=3.0)
        p.add_argument("-v", "--verbose", action="store_true")

    listing = sub.add_parser("list", help="list the patch's modules and their ids")
    add_connection_args(listing)
    listing.set_defaults(func=cmd_list)

    run = sub.add_parser("run", help="benchmark streaming overlay renders")
    add_connection_args(run)
    run.add_argument("--plugin", default="Fundamental", help="target module plugin slug")
    run.add_argument("--module", default="Scope", help="target module slug")
    run.add_argument("--index", type=int, default=0, help="which match, if several")
    run.add_argument(
        "--module-id", type=int, nargs="+", action="extend", metavar="ID",
        help="target module id(s); streams every listed overlay at once (overrides slugs)",
    )
    size = run.add_mutually_exclusive_group()
    size.add_argument("--height", type=int, default=512, help="render height in px")
    size.add_argument("--scale", type=float, help="render scale instead of height")
    run.add_argument("--width", type=int, help="render width in px (with --height)")
    run.add_argument("--rate", type=float, default=60, help="requests/sec; 0 = closed loop")
    run.add_argument("--duration", type=float, default=10, help="measured stream seconds")
    run.add_argument("--idle", type=float, default=3, help="idle baseline seconds")
    run.add_argument("--warmup", type=float, default=1, help="unmeasured stream seconds")
    run.add_argument("--timeout", type=float, default=2, help="per-frame/drain timeout")
    run.add_argument(
        "--no-overlay-cache", dest="overlay_cache", action="store_false",
        help="disable the overlay surrogate cache for this run (baseline)",
    )
    run.add_argument(
        "--inline-prep", dest="prep_worker", action="store_false",
        help="flip/compress on the UI thread instead of the prep worker",
    )
    run.add_argument(
        "--drop", type=float, default=0.0, metavar="P",
        help="simulate loss: discard this fraction of chunk packets unacked (0-1)",
    )
    run.add_argument(
        "--drop-acks", type=float, default=0.0, metavar="P",
        help="simulate loss: don't send this fraction of acks (0-1)",
    )
    run.add_argument("--label", default="", help="label stored in the JSON result")
    run.add_argument("--json", help="write results to this file")
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
