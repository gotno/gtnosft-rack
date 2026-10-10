#!/usr/bin/env python3
"""Minimal stand-in for OSCctrl, for testing overlay_bench.py without Rack.

Answers just enough of the protocol for `overlay_bench.py run` to complete:
registration and heartbeats, module stubs/state, the /bench/* routes (with a
canned report), and /get/texture with small fake chunked frames. Some texture
requests are dropped to imitate coalescing, and some chunks are dropped to
exercise the client's incomplete/retry handling. It never resends. The report
includes real ack/packet counts with fake timings, for the transport section.

The numbers it reports are fake; use it to check client behavior only.

Usage (always pass --host so the client can't discover a live Rack instead):
    .venv/bin/python mock_server.py &
    .venv/bin/python overlay_bench.py run --host 127.0.0.1 --duration 1 \\
        --idle 0.3 --warmup 0 --json /tmp/mock.json
"""

import argparse
import random
import socket

from pythonosc.osc_bundle_builder import IMMEDIATELY, OscBundleBuilder
from pythonosc.osc_message_builder import OscMessageBuilder
from pythonosc.osc_packet import OscPacket

CLIENT_PORT = 7746  # overlay_bench.py's listen port
CHUNK = 1300
TOGGLES = ("/bench/overlay_cache", "/bench/prep_worker")


def msg(addr, *args):
    b = OscMessageBuilder(address=addr)
    for t, v in args:
        b.add_arg(v, t)
    return b.build()


def stat(name, count, mean, mn, p50, p95, p99, mx):
    return msg(
        "/bench/stat", ("s", name), ("i", count),
        *[("f", float(x)) for x in (mean, mn, p50, p95, p99, mx)],
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", type=int, default=7225, help="listen port (OSCctrl's)")
    ap.add_argument("--coalesce-rate", type=float, default=0.3,
                    help="fraction of /get/texture requests silently dropped")
    ap.add_argument("--chunk-drop-rate", type=float, default=0.02,
                    help="fraction of texture chunks never sent")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", args.port))
    client = None
    acks = packets_sent = 0  # since the last /bench/reset
    print(f"mock OSCctrl listening on 127.0.0.1:{args.port}", flush=True)

    def send(*msgs):
        bb = OscBundleBuilder(IMMEDIATELY)
        for m in msgs:
            bb.add_content(m)
        sock.sendto(bb.build().dgram, (client[0], CLIENT_PORT))

    while True:
        data, addr = sock.recvfrom(65536)
        for tm in OscPacket(data).messages:
            a, p = tm.message.address, tm.message.params
            if a == "/register":
                client = addr
                send(msg("/heartbeat", ("f", 1.0), ("f", 2.0)))
            elif client is None:
                continue
            elif a == "/keepalive":
                send(msg("/heartbeat", ("f", 1.0), ("f", 2.0)))
            elif a in TOGGLES:
                print(a, p[0], flush=True)
                send(msg(a + "/ack", ("i", p[0])))
            elif a == "/ack_chunk":
                acks += 1
            elif a == "/bench/reset":
                acks = packets_sent = 0
                send(msg("/bench/reset/ack", ("h", 7)))
            elif a == "/get/module_stubs":
                send(
                    msg("/set/module_stub", ("h", 42), ("s", "Fundamental"), ("s", "Scope")),
                    msg("/set/module_stub", ("h", 43), ("s", "Fundamental"), ("s", "Scope")),
                    msg("/set/module_stub", ("h", 44), ("s", "Fundamental"), ("s", "VCO")),
                    msg("/report/module/count", ("h", 3)),
                )
            elif a == "/get/module_state":
                # overlay texture id = module id + 957 (module 42 -> 999)
                send(msg("/set/s/m", ("h", p[0]), ("f", 0.0), ("f", 0.0), ("h", p[0] + 957)))
            elif a == "/bench/report":
                ms = [
                    msg("/bench/report/begin", ("h", 7), ("f", 3.0)),
                    stat("overlay.draw", 10, 1, .5, 1, 2, 3, 4),
                    stat("overlay.request_to_first_send", 10, 3, 2, 3, 4, 5, 6),
                    stat("frame.interval", 100, 16, 15, 16, 17, 20, 30),
                    msg("/bench/counter", ("s", "overlay_cache.hit"), ("h", 10)),
                    # real counts, fake timings
                    msg("/bench/counter", ("s", "rx.acks"), ("h", acks)),
                    msg("/bench/counter", ("s", "rx.acks.busy_us"), ("h", acks * 2)),
                    msg("/bench/counter", ("s", "rx.packets"), ("h", acks)),
                    msg("/bench/counter", ("s", "rx.packets.busy_us"), ("h", acks * 3)),
                    msg("/bench/counter", ("s", "tx.packets"), ("h", packets_sent)),
                    msg("/bench/counter", ("s", "tx.packets.busy_us"), ("h", packets_sent * 5)),
                    msg("/bench/texture", ("h", 999), ("s", "overlay"),
                        ("i", 10), ("i", 0), ("f", 59.5)),
                ]
                ms.append(msg("/bench/report/end", ("i", len(ms) + 1)))
                send(*ms)
            elif a == "/get/texture":
                tid, seq, h = p[0], p[1], p[2]
                if random.random() < args.coalesce_rate:
                    continue
                if isinstance(h, float):  # scale request
                    h = 380
                w = max(1, h // 2)
                body = b"qoif" + w.to_bytes(4, "big") + h.to_bytes(4, "big") + bytes(4000)
                n = (len(body) + CHUNK - 1) // CHUNK
                for i in range(n):
                    if random.random() < args.chunk_drop_rate:
                        continue
                    part = body[i * CHUNK:(i + 1) * CHUNK]
                    packets_sent += 1
                    send(msg(
                        "/set/texture", ("h", tid), ("i", seq), ("i", i), ("i", n),
                        ("i", CHUNK), ("h", len(body)), ("i", w), ("i", h), ("b", part),
                    ))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
